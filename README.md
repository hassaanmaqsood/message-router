# MessageRouter — Hierarchical Message Routing System (JavaScript Port)

## Overview

`MessageRouter` is a **hierarchical message routing and dispatch system** designed for distributed or modular environments — such as:

* Embedded device communication (ESP32, IoT networks)
* Multi-module applications (e.g., firmware <-> WebSocket bridge)
* Frontend systems using modular event buses

It enables **structured message delivery** between endpoints identified by *path*, with features including:

* **Hierarchical message bubbling** (`/foo/bar` → `/foo` → `/`)
* **Path-based locks** with multiple lock modes (STRICT, EXCLUSIVE, etc.)
* **Per-path message queues** for locked routes
* **Listener lifecycle management** (add/remove/activate/deactivate)
* **Ownership-based locking and unlock permissions**
* **Message priority queuing and dispatch**

It is designed to be **deterministic**, **lightweight**, and **debug-friendly**, operating without any external libraries.

---

## Architectural Principles

1. **Path-based Hierarchy:**
   Every listener is attached to a normalized path.
   Messages propagate upward through parent paths unless stopped by a lock.

2. **Lock-aware Message Flow:**
   Paths can be dynamically locked to control message traffic between senders and receivers, enabling exclusive operations or temporary message suppression.

3. **Ownership Enforcement:**
   Locks and listeners are associated with owners, ensuring that only the correct module or subsystem can modify them.

4. **Queue Safety:**
   Messages targeting locked paths are queued and automatically delivered once the lock is lifted.

5. **Transparent State Introspection:**
   Full runtime introspection via `getStatus()` and `printStatus()` makes debugging easy.

---

## Core Components

### 1. Path Hierarchy

Paths follow a UNIX-like structure:

```
/                  -> root
/sensors           -> subsystem
/sensors/temp      -> specific component
```

Message bubbling occurs automatically upward:
`/sensors/temp` → `/sensors` → `/`

---

### 2. Listeners

A **listener** subscribes to a path and is notified when messages are routed to that path (or its descendants, via bubbling).

Each listener object contains:

```js
{
  id: number,
  path: '/example',
  callback: Function,
  ownerID: 'module-name',
  active: true,
  createdAt: timestamp
}
```

#### Methods:

* `addListener(path, callback, ownerID)`
* `removeListener(listenerId)`
* `setListenerActive(id, active)`
* `removeListenersByPath(path)`
* `removeListenersByOwner(ownerID)`
* `getListenerInfo(id)`
* `getListenerIds(path)`
* `getListenerCount(path)`
* `getActiveListenerCount()`

---

### 3. Messages

Messages are transient objects containing:

```js
{
  path: '/sensors/temp',
  data: '25.3°C',
  senderID: 'sensor01',
  receiverID: 'display01',
  timestamp: 1730989200000,
  messageId: 34
}
```

Messages can be:

* **Sent immediately** via `sendMessage()`
* **Queued** for later via `queueMessage()`
* **Automatically dispatched** when a locked path is unlocked

---

### 4. Path Locks

Locks control message flow through specific paths.
Each lock can restrict message routing based on `senderID`, `receiverID`, and mode.

#### Lock Modes

| Mode           | Description                                                 |
| -------------- | ----------------------------------------------------------- |
| `STRICT`       | Only messages matching exact sender & receiver IDs allowed. |
| `EXCLUSIVE`    | All other sender/receiver pairs are blocked.                |
| `ANY_SENDER`   | Locks receiver; any sender allowed.                         |
| `ANY_RECEIVER` | Locks sender; any receiver allowed.                         |

#### Methods

* `lockPath(path, mode, ownerID, senderID?, receiverID?)`
* `unlockPath(path, ownerID)`
* `finalizeLock(path, ownerID)`
* `getLockStatus(path)`
* `clearLocks()`

#### Lifecycle

1. **Lock creation**

   * Initializes lock entry in `pathLocks`
   * Creates a queue in `lockedPathQueues`

2. **During lock**

   * Incoming messages are validated by `validateMessageLock()`
   * Invalid messages are blocked
   * Queued if `queueIfLocked=true`

3. **Unlocking**

   * All queued messages are delivered in order

4. **Finalization**

   * Lock becomes immutable (cannot be modified or removed)

---

### 5. Queues

#### Message Queue

Global message queue for non-locked paths, with priority support.

* Maximum capacity: `MAX_QUEUE_SIZE = 50`
* Oldest message is dropped when full

#### Locked Path Queues

Each locked path has its own message queue (`lockedPathQueues`).

#### Methods:

* `queueMessage(path, data, senderID, receiverID, priority)`
* `processQueue()` – processes and clears main queue
* `getQueueSize()`
* `getLockedQueueSize()`
* `clearQueue()`

---

## Detailed Method Documentation

### `normalizePath(path: string): string`

Ensures all paths are consistent:

* Begins with `/`
* Has no trailing `/` (except root)

```js
normalizePath('sensors/temp/') // '/sensors/temp'
normalizePath('temp')          // '/temp'
```

---

### `getParentPath(path: string): string`

Returns the immediate parent path.
Useful for hierarchical bubbling.

```js
getParentPath('/a/b/c') // '/a/b'
getParentPath('/a')     // '/'
```

---

### `getBubblingPaths(path: string): string[]`

Generates the full bubble chain for a path.

```js
getBubblingPaths('/a/b/c')
// ['/a/b/c', '/a/b', '/a', '/']
```

---

### `validateMessageLock(message): boolean`

Determines if a message passes current path lock constraints.
Returns `true` if allowed, `false` if blocked.

---

### `processMessageImmediate(message, senderPath = ''): boolean`

Routes a message to all active listeners whose paths match or bubble from the target path.

Steps:

1. Normalizes path
2. Checks lock validity
3. Dispatches message to all listeners with matching path
4. Calls listener callback `(message, path, senderID)`

Returns `true` if processed, `false` if blocked.

---

### `sendMessage(path, data, senderID, receiverID, queueIfLocked = true): boolean`

Main entry point for sending messages.

Behavior:

* If path is unlocked → immediate dispatch
* If locked and `queueIfLocked` → queued
* If locked and `!queueIfLocked` → dropped

Returns `true` if sent immediately, `false` if queued/dropped.

---

### `unlockPath(path, ownerID): boolean`

Removes a lock if ownership matches and replays all queued messages.

---

### `getStatus(): string`

Returns a detailed string representation of:

* Listener count
* Queue sizes
* Path locks (with mode, sender/receiver IDs)
* Active listeners (with owner, timestamp)

---

### `printStatus(): void`

Prints formatted router state for debugging.

---

## Example Usage

```js
const router = new MessageRouter();

// Create listeners
router.addListener('/', (msg) => console.log('Root received:', msg.data), 'system');
router.addListener('/sensors', (msg) => console.log('Sensor event:', msg.data), 'sensor-manager');

// Lock temperature path for controlled access
router.lockPath('/sensors/temp', MessageRouter.LockMode.STRICT, 'sensor-manager', 'temp-sensor', 'display01');

// Send message (will queue)
router.sendMessage('/sensors/temp', '25.6°C', 'temp-sensor', 'display01');

// Unlock path and flush queue
router.unlockPath('/sensors/temp', 'sensor-manager');

// Print router state
router.printStatus();
```

---

## 🧩 Internal Data Structures

### `listeners`

Array of listener objects
→ used for matching message targets

### `pathLocks`

`Map<normalizedPath, LockObject>`
→ defines routing restrictions

### `lockedPathQueues`

`Map<normalizedPath, Message[]>`
→ holds deferred messages awaiting unlock

### `messageQueue`

Array of queued messages (not tied to locks)

---

## Safety & Reliability

* **Fail-safe processing:**
  Listener exceptions are caught and logged without interrupting dispatch.
* **Message dropping:**
  Oldest messages are dropped when the global queue exceeds capacity.
* **Lock enforcement:**
  Even if a path is locked, message integrity is preserved in a queued state.
* **Ownership enforcement:**
  Locks can only be released by their owners.

---

## 🔍 Debugging & Introspection

The `getStatus()` and `printStatus()` methods give full insight into:

* Listeners (count, owners, active state)
* Message queues
* Path locks
* Pending messages per locked path

Example output:

```
=== Message Router Status ===
Listeners: 3 (active: 3)
Queue size: 0
Locked path queues: 1 messages
Path locks: 1

Locks detail:
  Path: /sensors/temp, Mode: STRICT
    Sender: temp-sensor, Receiver: display01
    Finalized: No, Queued messages: 1

Active Listeners:
  ID: 1
    Path: /
    Owner: system
    Active: Yes
    Created At: 2025-11-07T17:52:00.000Z
```

---

## Design Extensions (Future)

* **Async delivery with Promises**
* **Wildcard paths (`/sensors/*`)**
* **Lock expiration timeouts**
* **Persistent routing state**
* **Network synchronization between routers**

---

## 🪶 Summary

| Feature              | Description                             |
| -------------------- | --------------------------------------- |
| Hierarchical Routing | Automatic bubbling through parent paths |
| Path Locking         | Multi-mode lock control with ownership  |
| Queued Messaging     | Deferred delivery for locked routes     |
| Listener Management  | Add/remove/activate/deactivate          |
| Ownership Model      | Restricts lock manipulation             |
| Debug Tools          | Real-time introspection and logging     |

---

Would you like me to format this into a **Markdown developer manual** (e.g. `MESSAGE_ROUTER.md`) or **JSDoc inline comments** that could be used in code editors and documentation generators?
