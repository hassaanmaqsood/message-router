/**
 * MessageRouter - A hierarchical message routing system with path locking
 * JavaScript port with enhanced queue management for locked paths
 */

class MessageRouter {
  // Lock modes for path control
  static LockMode = {
    STRICT: 'STRICT',           // Only exact sender and receiver IDs allowed
    EXCLUSIVE: 'EXCLUSIVE',     // No other sender/receiver allowed
    ANY_SENDER: 'ANY_SENDER',   // Only receiver is locked
    ANY_RECEIVER: 'ANY_RECEIVER' // Only sender is locked
  };

  constructor() {
    this.listeners = [];
    this.messageQueue = [];
    this.pathLocks = new Map();
    this.lockedPathQueues = new Map(); // Queue for messages blocked by locks
    this.nextListenerId = 1;
    this.nextMessageId = 1;
    this.MAX_QUEUE_SIZE = 50;
  }

  /**
   * Normalize path format
   */
  normalizePath(path) {
    let normalized = path;

    // Remove trailing slash except for root
    if (normalized.length > 1 && normalized.endsWith('/')) {
      normalized = normalized.slice(0, -1);
    }

    // Ensure path starts with /
    if (!normalized || normalized[0] !== '/') {
      normalized = '/' + normalized;
    }

    return normalized;
  }

  /**
   * Get parent path for bubbling
   */
  getParentPath(path) {
    if (path === '/') return '';

    const lastSlash = path.lastIndexOf('/');
    if (lastSlash === -1 || lastSlash === 0) return '/';

    return path.substring(0, lastSlash);
  }

  /**
   * Get all paths that should receive the message (bubbling)
   */
  getBubblingPaths(path) {
    const paths = [];
    let currentPath = this.normalizePath(path);

    paths.push(currentPath);

    while (currentPath !== '/') {
      currentPath = this.getParentPath(currentPath);
      if (currentPath) {
        paths.push(currentPath);
      }
    }

    return paths;
  }

  /**
   * Validate message against path locks
   */
  validateMessageLock(message) {
    const normalizedPath = this.normalizePath(message.path);

    // Check if path is locked
    const lock = this.pathLocks.get(normalizedPath);
    if (!lock) {
      return true; // No lock, message allowed
    }

    switch (lock.mode) {
      case MessageRouter.LockMode.STRICT:
        // Must match exact sender and receiver IDs
        if (lock.senderID && lock.senderID !== message.senderID) return false;
        if (lock.receiverID && lock.receiverID !== message.receiverID) return false;
        break;

      case MessageRouter.LockMode.EXCLUSIVE:
        // No other sender/receiver allowed if lock is set
        if (lock.senderID && lock.senderID !== message.senderID) return false;
        if (lock.receiverID && lock.receiverID !== message.receiverID) return false;
        break;

      case MessageRouter.LockMode.ANY_SENDER:
        // Only receiver is locked
        if (lock.receiverID && lock.receiverID !== message.receiverID) return false;
        break;

      case MessageRouter.LockMode.ANY_RECEIVER:
        // Only sender is locked
        if (lock.senderID && lock.senderID !== message.senderID) return false;
        break;
    }

    return true;
  }

  /**
   * Process a single message immediately
   */
  processMessageImmediate(message, senderPath = '') {
    const normalizedPath = this.normalizePath(message.path);
    
    // Check if path is locked
    if (this.pathLocks.has(normalizedPath)) {
      console.warn(`[MessageRouter] Message blocked by path lock: ${message.path}`);
      return false;
    }

    // Validate against path locks
    if (!this.validateMessageLock(message)) {
      console.warn(`[MessageRouter] Message blocked by path lock validation: ${message.path}`);
      return false;
    }

    const targetPaths = this.getBubblingPaths(message.path);

    // Send to all matching active listeners
    for (const targetPath of targetPaths) {
      for (const listener of this.listeners) {
        if (listener.path === targetPath && listener.active) {
          // Check if message is targeted to specific receiver
          if (message.receiverID) {
            if (listener.id.toString() !== message.receiverID) {
              continue; // Skip if not the intended receiver
            }
          }

          try {
            // Call callback with enhanced parameters: (message, path, senderID)
            listener.callback(message, targetPath, message.senderID);
          } catch (error) {
            console.warn('[MessageRouter] Listener callback threw exception:', error);
          }
        }
      }
    }

    return true;
  }

  /**
   * Add a listener to a path
   * @param {string} path - The path to listen on
   * @param {Function} callback - Callback function (message, path, senderID) => void
   * @param {string} ownerID - Optional owner identifier
   * @returns {number} Listener ID
   */
  addListener(path, callback, ownerID = '') {
    const normalizedPath = this.normalizePath(path);
    const id = this.nextListenerId++;

    const listener = {
      path: normalizedPath,
      callback,
      id,
      ownerID,
      active: true,
      createdAt: Date.now()
    };

    this.listeners.push(listener);

    console.log(`[MessageRouter] Listener ${id} added to path: ${normalizedPath} (owner: ${ownerID})`);
    return id;
  }

  /**
   * Remove listener by ID
   */
  removeListener(listenerId) {
    const index = this.listeners.findIndex(l => l.id === listenerId);
    if (index !== -1) {
      const listener = this.listeners[index];
      console.log(`[MessageRouter] Listener ${listenerId} removed from path: ${listener.path}`);
      this.listeners.splice(index, 1);
      return true;
    }
    return false;
  }

  /**
   * Remove multiple listeners by ID
   */
  removeListeners(listenerIds) {
    let removedCount = 0;
    for (const id of listenerIds) {
      if (this.removeListener(id)) {
        removedCount++;
      }
    }
    return removedCount;
  }

  /**
   * Remove all listeners from a specific path
   */
  removeListenersByPath(path) {
    const normalizedPath = this.normalizePath(path);
    const initialLength = this.listeners.length;
    
    this.listeners = this.listeners.filter(listener => {
      if (listener.path === normalizedPath) {
        console.log(`[MessageRouter] Listener ${listener.id} removed from path: ${normalizedPath}`);
        return false;
      }
      return true;
    });

    return initialLength - this.listeners.length;
  }

  /**
   * Remove listeners by owner ID
   */
  removeListenersByOwner(ownerID) {
    const initialLength = this.listeners.length;
    
    this.listeners = this.listeners.filter(listener => {
      if (listener.ownerID === ownerID) {
        console.log(`[MessageRouter] Listener ${listener.id} removed (owner: ${ownerID})`);
        return false;
      }
      return true;
    });

    return initialLength - this.listeners.length;
  }

  /**
   * Activate/Deactivate listener without removing
   */
  setListenerActive(listenerId, active) {
    const listener = this.listeners.find(l => l.id === listenerId);
    if (listener) {
      listener.active = active;
      console.log(`[MessageRouter] Listener ${listenerId} ${active ? 'activated' : 'deactivated'}`);
      return true;
    }
    return false;
  }

  /**
   * Lock a path with specified mode and ownership
   * @param {string} path - Path to lock
   * @param {string} mode - Lock mode from MessageRouter.LockMode
   * @param {string} ownerID - Owner identifier
   * @param {string} senderID - Optional sender ID restriction
   * @param {string} receiverID - Optional receiver ID restriction
   */
  lockPath(path, mode, ownerID, senderID = '', receiverID = '') {
    const normalizedPath = this.normalizePath(path);

    // Check if path is already locked and finalized
    const existing = this.pathLocks.get(normalizedPath);
    if (existing && existing.finalized) {
      console.warn(`[MessageRouter] Path ${normalizedPath} is already finalized and cannot be modified`);
      return false;
    }

    const lock = {
      path: normalizedPath,
      mode,
      senderID,
      senderOwner: senderID ? ownerID : '',
      receiverID,
      receiverOwner: receiverID ? ownerID : '',
      finalized: false,
      timestamp: Date.now()
    };

    this.pathLocks.set(normalizedPath, lock);
    
    // Initialize queue for this locked path if it doesn't exist
    if (!this.lockedPathQueues.has(normalizedPath)) {
      this.lockedPathQueues.set(normalizedPath, []);
    }

    console.log(`[MessageRouter] Path locked: ${normalizedPath} (mode: ${mode}, owner: ${ownerID})`);
    return true;
  }

  /**
   * Unlock a path and process queued messages
   * @param {string} path - Path to unlock
   * @param {string} ownerID - Owner identifier
   */
  unlockPath(path, ownerID) {
    const normalizedPath = this.normalizePath(path);

    const lock = this.pathLocks.get(normalizedPath);
    if (!lock) {
      return false; // Path not locked
    }

    if (lock.finalized) {
      console.warn(`[MessageRouter] Cannot unlock finalized path: ${normalizedPath}`);
      return false;
    }

    // Check ownership
    if (lock.senderOwner !== ownerID && lock.receiverOwner !== ownerID) {
      console.warn(`[MessageRouter] Access denied: ${ownerID} cannot unlock path ${normalizedPath}`);
      return false;
    }

    // Remove the lock
    this.pathLocks.delete(normalizedPath);

    // Process queued messages for this path
    const queuedMessages = this.lockedPathQueues.get(normalizedPath) || [];
    console.log(`[MessageRouter] Path unlocked: ${normalizedPath} by ${ownerID}. Processing ${queuedMessages.length} queued messages.`);

    // Process all queued messages
    for (const queuedMsg of queuedMessages) {
      this.processMessageImmediate(queuedMsg.message, queuedMsg.senderPath);
    }

    // Clear the queue for this path
    this.lockedPathQueues.delete(normalizedPath);

    return true;
  }

  /**
   * Finalize a path lock (makes it permanent)
   */
  finalizeLock(path, ownerID) {
    const normalizedPath = this.normalizePath(path);

    const lock = this.pathLocks.get(normalizedPath);
    if (!lock) {
      return false;
    }

    // Check ownership
    if (lock.senderOwner !== ownerID && lock.receiverOwner !== ownerID) {
      return false;
    }

    lock.finalized = true;
    console.log(`[MessageRouter] Path lock finalized: ${normalizedPath}`);
    return true;
  }

  /**
   * Get lock status for a path
   */
  getLockStatus(path) {
    const normalizedPath = this.normalizePath(path);
    return this.pathLocks.get(normalizedPath) || null;
  }

  /**
   * Send message immediately with enhanced parameters
   * @param {string} path - Message path
   * @param {string} data - Message data
   * @param {string} senderID - Sender identifier
   * @param {string} receiverID - Receiver identifier
   * @param {boolean} queueIfLocked - If true, queue message if path is locked; if false, drop it
   */
  sendMessage(path, data, senderID = '', receiverID = '', queueIfLocked = true) {
    const normalizedPath = this.normalizePath(path);
    
    const message = {
      path: normalizedPath,
      data,
      senderID,
      receiverID,
      timestamp: Date.now(),
      messageId: this.nextMessageId++
    };

    // Check if path is locked
    if (this.pathLocks.has(normalizedPath)) {
      if (queueIfLocked) {
        // Queue the message to be sent when path is unlocked
        const queue = this.lockedPathQueues.get(normalizedPath) || [];
        queue.push({ message, senderPath: '', priority: 0 });
        this.lockedPathQueues.set(normalizedPath, queue);
        console.log(`[MessageRouter] Message queued due to path lock: ${normalizedPath}`);
        return false;
      } else {
        // Drop the message
        console.warn(`[MessageRouter] Message dropped due to path lock: ${normalizedPath}`);
        return false;
      }
    }

    return this.processMessageImmediate(message);
  }

  /**
   * Queue message for later processing
   */
  queueMessage(path, data, senderID = '', receiverID = '', priority = 0) {
    if (this.messageQueue.length >= this.MAX_QUEUE_SIZE) {
      console.warn('[MessageRouter] Message queue full, dropping oldest message');
      this.messageQueue.shift();
    }

    const message = {
      path: this.normalizePath(path),
      data,
      senderID,
      receiverID,
      timestamp: Date.now(),
      messageId: this.nextMessageId++
    };

    this.messageQueue.push({
      message,
      senderPath: '',
      priority
    });

    return true;
  }

  /**
   * Process queued messages (respects priority)
   */
  processQueue() {
    // Sort by priority (higher priority first)
    this.messageQueue.sort((a, b) => b.priority - a.priority);

    while (this.messageQueue.length > 0) {
      const queuedMsg = this.messageQueue.shift();
      this.processMessageImmediate(queuedMsg.message, queuedMsg.senderPath);
    }
  }

  /**
   * Get listener information by ID
   */
  getListenerInfo(listenerId) {
    const listener = this.listeners.find(l => l.id === listenerId);
    if (listener) {
      return {
        path: listener.path,
        ownerID: listener.ownerID,
        active: listener.active
      };
    }
    return null;
  }

  /**
   * Get all listener IDs for a path
   */
  getListenerIds(path) {
    const normalizedPath = this.normalizePath(path);
    return this.listeners
      .filter(l => l.path === normalizedPath)
      .map(l => l.id);
  }

  /**
   * Get queue size
   */
  getQueueSize() {
    return this.messageQueue.length;
  }

  /**
   * Get total queued messages in locked path queues
   */
  getLockedQueueSize() {
    let total = 0;
    for (const queue of this.lockedPathQueues.values()) {
      total += queue.length;
    }
    return total;
  }

  /**
   * Get listener count
   */
  getListenerCount(path = null) {
    if (path === null) {
      return this.listeners.length;
    }

    const normalizedPath = this.normalizePath(path);
    return this.listeners.filter(l => l.path === normalizedPath && l.active).length;
  }

  /**
   * Clear all listeners
   */
  clearListeners() {
    this.listeners = [];
    console.log('[MessageRouter] All listeners cleared');
  }

  /**
   * Clear message queue
   */
  clearQueue() {
    this.messageQueue = [];
    console.log('[MessageRouter] Message queue cleared');
  }

  /**
   * Clear all path locks and their queues
   */
  clearLocks() {
    this.pathLocks.clear();
    this.lockedPathQueues.clear();
    console.log('[MessageRouter] All path locks cleared');
  }

  /**
   * Get active listener count
   */
  getActiveListenerCount() {
    return this.listeners.filter(l => l.active).length;
  }

  /**
   * Get comprehensive status
   */
  getStatus() {
    let status = '';
    status += `Listeners: ${this.listeners.length} (active: ${this.getActiveListenerCount()})\n`;
    status += `Queue size: ${this.messageQueue.length}\n`;
    status += `Locked path queues: ${this.getLockedQueueSize()} messages\n`;
    status += `Path locks: ${this.pathLocks.size}\n`;
    status += `Next listener ID: ${this.nextListenerId}\n`;
    status += `Next message ID: ${this.nextMessageId}\n\n`;

    if (this.pathLocks.size > 0) {
      status += 'Locks detail:\n';
      for (const [path, lock] of this.pathLocks) {
        const queueSize = (this.lockedPathQueues.get(path) || []).length;
        status += `  Path: ${lock.path}, Mode: ${lock.mode}\n`;
        status += `    Sender: ${lock.senderID || 'N/A'}, Receiver: ${lock.receiverID || 'N/A'}\n`;
        status += `    Finalized: ${lock.finalized ? 'Yes' : 'No'}, Queued messages: ${queueSize}\n`;
      }
      status += '\n';
    } else {
      status += 'No active path locks.\n\n';
    }

    if (this.listeners.length > 0) {
      status += 'Active Listeners:\n';
      for (const listener of this.listeners) {
        status += `  ID: ${listener.id}\n`;
        status += `    Path: ${listener.path}\n`;
        status += `    Owner: ${listener.ownerID || 'N/A'}\n`;
        status += `    Active: ${listener.active ? 'Yes' : 'No'}\n`;
        status += `    Created At: ${new Date(listener.createdAt).toISOString()}\n`;
      }
    } else {
      status += 'No active listeners.\n';
    }

    return status;
  }

  /**
   * Print status to console
   */
  printStatus() {
    console.log('=== Message Router Status ===\n' + this.getStatus());
  }
}

// Example usage
/*
const router = new MessageRouter();

// Add listeners
const rootId = router.addListener('/', (msg, path, senderID) => {
  console.log(`[ROOT] From ${senderID}: ${msg.path} -> ${msg.data}`);
}, 'system');

const sensorId = router.addListener('/sensors', (msg, path, senderID) => {
  console.log(`[SENSORS] From ${senderID}: ${msg.path} -> ${msg.data}`);
}, 'sensor-manager');

const tempId = router.addListener('/sensors/temperature', (msg, path, senderID) => {
  console.log(`[TEMP] From ${senderID}: ${msg.path} -> ${msg.data}`);
}, 'temp-sensor');

// Lock a path
router.lockPath('/sensors/temperature', MessageRouter.LockMode.STRICT, 
  'temp-sensor', 'temp-device-01', tempId.toString());

// Send messages - they will be queued
router.sendMessage('/sensors/temperature', '25.6°C', 'temp-device-01', tempId.toString());

// Unlock path - queued messages will be processed
router.unlockPath('/sensors/temperature', 'temp-sensor');

// Send without queuing if locked
router.sendMessage('/sensors/temperature', '26.1°C', 'temp-device-01', tempId.toString(), false);

router.printStatus();
*/

// Export for use in Node.js or modules
if (typeof module !== 'undefined' && module.exports) {
  module.exports = MessageRouter;
}
