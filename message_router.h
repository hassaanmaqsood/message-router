#ifndef MESSAGE_ROUTER_H
#define MESSAGE_ROUTER_H

#include <Arduino.h>
#include <vector>
#include <functional>
#include <map>

class MessageRouter
{
public:
    // Lock modes for path control
    enum class LockMode
    {
        STRICT,      // Only exact sender and receiver IDs allowed
        EXCLUSIVE,   // No other sender/receiver allowed
        ANY_SENDER,  // Only receiver is locked
        ANY_RECEIVER // Only sender is locked
    };

    // Path lock structure
    struct PathLock
    {
        String path;
        LockMode mode;
        String senderID;
        String senderOwner;
        String receiverID;
        String receiverOwner;
        bool finalized;
        unsigned long timestamp;

        PathLock() : mode(LockMode::STRICT), finalized(false), timestamp(0) {}
        
        PathLock(const String &p, LockMode m, const String &senderId = "", const String &receiverId = "")
            : path(p), mode(m), senderID(senderId), receiverID(receiverId), 
              finalized(false), timestamp(millis()) {}
    };

    // Enhanced message structure with sender information
    struct Message
    {
        String path;
        String data;
        String senderID;
        String receiverID;
        unsigned long timestamp;
        uint32_t messageId;

        Message(const String &p, const String &d, const String &sender = "", const String &receiver = "")
            : path(p), data(d), senderID(sender), receiverID(receiver), 
              timestamp(millis()), messageId(0) {}
    };

    // Enhanced callback function type with sender information
    typedef std::function<void(const Message &, const String &, const String &)> MessageCallback;

private:
    // Enhanced listener structure with ID and metadata
    struct Listener
    {
        String path;
        MessageCallback callback;
        uint32_t id;
        String ownerID;
        bool active;
        unsigned long createdAt;

        Listener(const String &p, MessageCallback cb, uint32_t i, const String &owner = "")
            : path(p), callback(cb), id(i), ownerID(owner), active(true), createdAt(millis()) {}
    };

    // Enhanced message queue structure
    struct QueuedMessage
    {
        Message message;
        String senderPath;
        uint32_t priority;

        QueuedMessage(const Message &msg, const String &sender, uint32_t prio = 0)
            : message(msg), senderPath(sender), priority(prio) {}
    };

    std::vector<Listener> listeners;
    std::vector<QueuedMessage> messageQueue;
    std::map<String, PathLock> pathLocks;
    uint32_t nextListenerId;
    uint32_t nextMessageId;
    static const size_t MAX_QUEUE_SIZE = 50;

    // Helper function to normalize paths
    String normalizePath(const String &path)
    {
        String normalized = path;

        // Remove trailing slash except for root
        if (normalized.length() > 1 && normalized.endsWith("/"))
        {
            normalized = normalized.substring(0, normalized.length() - 1);
        }

        // Ensure path starts with /
        if (!normalized.startsWith("/"))
        {
            normalized = "/" + normalized;
        }

        return normalized;
    }

    // Get parent path for bubbling
    String getParentPath(const String &path)
    {
        if (path == "/")
            return "";

        int lastSlash = path.lastIndexOf('/');
        if (lastSlash <= 0)
            return "/";

        return path.substring(0, lastSlash);
    }

    // Get all paths that should receive the message
    std::vector<String> getBubblingPaths(const String &path)
    {
        std::vector<String> paths;
        String currentPath = normalizePath(path);

        paths.push_back(currentPath);

        while (currentPath != "/")
        {
            currentPath = getParentPath(currentPath);
            if (!currentPath.isEmpty())
            {
                paths.push_back(currentPath);
            }
        }

        return paths;
    }

    // Validate message against path locks
    bool validateMessageLock(const Message &message)
    {
        String normalizedPath = normalizePath(message.path);
        
        // Check if path is locked
        auto lockIt = pathLocks.find(normalizedPath);
        if (lockIt == pathLocks.end())
        {
            return true; // No lock, message allowed
        }

        const PathLock &lock = lockIt->second;

        switch (lock.mode)
        {
        case LockMode::STRICT:
            // Must match exact sender and receiver IDs
            if (!lock.senderID.isEmpty() && lock.senderID != message.senderID)
                return false;
            if (!lock.receiverID.isEmpty() && lock.receiverID != message.receiverID)
                return false;
            break;

        case LockMode::EXCLUSIVE:
            // No other sender/receiver allowed if lock is set
            if (!lock.senderID.isEmpty() && lock.senderID != message.senderID)
                return false;
            if (!lock.receiverID.isEmpty() && lock.receiverID != message.receiverID)
                return false;
            break;

        case LockMode::ANY_SENDER:
            // Only receiver is locked
            if (!lock.receiverID.isEmpty() && lock.receiverID != message.receiverID)
                return false;
            break;

        case LockMode::ANY_RECEIVER:
            // Only sender is locked
            if (!lock.senderID.isEmpty() && lock.senderID != message.senderID)
                return false;
            break;
        }

        return true;
    }

    // Process a single message immediately
    void processMessageImmediate(const Message &message, const String &senderPath = "")
    {
        // Validate against path locks
        if (!validateMessageLock(message))
        {
            Serial.printf("Message blocked by path lock: %s\n", message.path.c_str());
            return;
        }

        std::vector<String> targetPaths = getBubblingPaths(message.path);

        // Send to all matching active listeners
        for (const String &targetPath : targetPaths)
        {
            for (const Listener &listener : listeners)
            {
                if (listener.path == targetPath && listener.active)
                {
                    // Check if message is targeted to specific receiver
                    if (!message.receiverID.isEmpty() && 
                        String(listener.id) != message.receiverID)
                    {
                        continue; // Skip if not the intended receiver
                    }

                    try
                    {
                        // Call callback with enhanced parameters: (message, path, senderID)
                        listener.callback(message, targetPath, message.senderID);
                    }
                    catch (...)
                    {
                        Serial.println("Warning: Listener callback threw exception");
                    }
                }
            }
        }
    }

public:
    MessageRouter() : nextListenerId(1), nextMessageId(1) {}

    // Enhanced listener management with ID-based operations
    uint32_t addListener(const String &path, MessageCallback callback, const String &ownerID = "")
    {
        String normalizedPath = normalizePath(path);
        uint32_t id = nextListenerId++;

        listeners.emplace_back(normalizedPath, callback, id, ownerID);

        Serial.printf("Listener %u added to path: %s (owner: %s)\n", 
                     id, normalizedPath.c_str(), ownerID.c_str());
        return id;
    }

    // Remove listener by ID
    bool removeListener(uint32_t listenerId)
    {
        for (auto it = listeners.begin(); it != listeners.end(); ++it)
        {
            if (it->id == listenerId)
            {
                Serial.printf("Listener %u removed from path: %s\n", listenerId, it->path.c_str());
                listeners.erase(it);
                return true;
            }
        }
        return false;
    }

    // Remove multiple listeners by ID
    int removeListeners(const std::vector<uint32_t> &listenerIds)
    {
        int removedCount = 0;
        
        for (uint32_t id : listenerIds)
        {
            if (removeListener(id))
            {
                removedCount++;
            }
        }
        
        return removedCount;
    }

    // Remove all listeners from a specific path
    int removeListenersByPath(const String &path)
    {
        String normalizedPath = normalizePath(path);
        int removedCount = 0;

        auto it = listeners.begin();
        while (it != listeners.end())
        {
            if (it->path == normalizedPath)
            {
                Serial.printf("Listener %u removed from path: %s\n", it->id, normalizedPath.c_str());
                it = listeners.erase(it);
                removedCount++;
            }
            else
            {
                ++it;
            }
        }

        return removedCount;
    }

    // Remove listeners by owner ID
    int removeListenersByOwner(const String &ownerID)
    {
        int removedCount = 0;

        auto it = listeners.begin();
        while (it != listeners.end())
        {
            if (it->ownerID == ownerID)
            {
                Serial.printf("Listener %u removed (owner: %s)\n", it->id, ownerID.c_str());
                it = listeners.erase(it);
                removedCount++;
            }
            else
            {
                ++it;
            }
        }

        return removedCount;
    }

    // Activate/Deactivate listener without removing
    bool setListenerActive(uint32_t listenerId, bool active)
    {
        for (Listener &listener : listeners)
        {
            if (listener.id == listenerId)
            {
                listener.active = active;
                Serial.printf("Listener %u %s\n", listenerId, active ? "activated" : "deactivated");
                return true;
            }
        }
        return false;
    }

    // PATH LOCKING MECHANISM

    // Lock a path with specified mode and ownership
    bool lockPath(const String &path, LockMode mode, const String &ownerID,
                  const String &senderID = "", const String &receiverID = "")
    {
        String normalizedPath = normalizePath(path);

        // Check if path is already locked
        auto existing = pathLocks.find(normalizedPath);
        if (existing != pathLocks.end() && existing->second.finalized)
        {
            Serial.printf("Path %s is already finalized and cannot be modified\n", normalizedPath.c_str());
            return false;
        }

        PathLock lock(normalizedPath, mode, senderID, receiverID);
        
        // Set appropriate owner based on what's being locked
        if (!senderID.isEmpty())
        {
            lock.senderOwner = ownerID;
        }
        if (!receiverID.isEmpty())
        {
            lock.receiverOwner = ownerID;
        }

        pathLocks[normalizedPath] = lock;
        
        Serial.printf("Path locked: %s (mode: %d, owner: %s)\n", 
                     normalizedPath.c_str(), (int)mode, ownerID.c_str());
        return true;
    }

    // Unlock a path (only by owner)
    bool unlockPath(const String &path, const String &ownerID)
    {
        String normalizedPath = normalizePath(path);
        
        auto it = pathLocks.find(normalizedPath);
        if (it == pathLocks.end())
        {
            return false; // Path not locked
        }

        PathLock &lock = it->second;
        
        if (lock.finalized)
        {
            Serial.printf("Cannot unlock finalized path: %s\n", normalizedPath.c_str());
            return false;
        }

        // Check ownership
        if (lock.senderOwner != ownerID && lock.receiverOwner != ownerID)
        {
            Serial.printf("Access denied: %s cannot unlock path %s\n", 
                         ownerID.c_str(), normalizedPath.c_str());
            return false;
        }

        pathLocks.erase(it);
        Serial.printf("Path unlocked: %s by %s\n", normalizedPath.c_str(), ownerID.c_str());
        return true;
    }

    // Finalize a path lock (makes it permanent)
    bool finalizeLock(const String &path, const String &ownerID)
    {
        String normalizedPath = normalizePath(path);
        
        auto it = pathLocks.find(normalizedPath);
        if (it == pathLocks.end())
        {
            return false;
        }

        PathLock &lock = it->second;
        
        // Check ownership
        if (lock.senderOwner != ownerID && lock.receiverOwner != ownerID)
        {
            return false;
        }

        lock.finalized = true;
        Serial.printf("Path lock finalized: %s\n", normalizedPath.c_str());
        return true;
    }

    // Get lock status for a path
    PathLock* getLockStatus(const String &path)
    {
        String normalizedPath = normalizePath(path);
        auto it = pathLocks.find(normalizedPath);
        return (it != pathLocks.end()) ? &it->second : nullptr;
    }

    // ENHANCED MESSAGE SENDING

    // Send message immediately with enhanced parameters
    void sendMessage(const String &path, const String &data, 
                    const String &senderID = "", const String &receiverID = "")
    {
        Message message(normalizePath(path), data, senderID, receiverID);
        message.messageId = nextMessageId++;
        processMessageImmediate(message);
    }

    // Queue message for later processing
    bool queueMessage(const String &path, const String &data, 
                     const String &senderID = "", const String &receiverID = "", 
                     uint32_t priority = 0)
    {
        if (messageQueue.size() >= MAX_QUEUE_SIZE)
        {
            Serial.println("Warning: Message queue full, dropping oldest message");
            messageQueue.erase(messageQueue.begin());
        }

        Message message(normalizePath(path), data, senderID, receiverID);
        message.messageId = nextMessageId++;
        messageQueue.emplace_back(message, "", priority);
        return true;
    }

    // Process queued messages (respects priority)
    void processQueue()
    {
        // Sort by priority (higher priority first)
        std::sort(messageQueue.begin(), messageQueue.end(),
                 [](const QueuedMessage &a, const QueuedMessage &b) {
                     return a.priority > b.priority;
                 });

        while (!messageQueue.empty())
        {
            QueuedMessage queuedMsg = messageQueue.front();
            messageQueue.erase(messageQueue.begin());

            processMessageImmediate(queuedMsg.message, queuedMsg.senderPath);
        }
    }

    // UTILITY AND DEBUG FUNCTIONS

    // Get listener information by ID
    bool getListenerInfo(uint32_t listenerId, String &path, String &ownerID, bool &active)
    {
        for (const Listener &listener : listeners)
        {
            if (listener.id == listenerId)
            {
                path = listener.path;
                ownerID = listener.ownerID;
                active = listener.active;
                return true;
            }
        }
        return false;
    }

    // Get all listener IDs for a path
    std::vector<uint32_t> getListenerIds(const String &path)
    {
        String normalizedPath = normalizePath(path);
        std::vector<uint32_t> ids;

        for (const Listener &listener : listeners)
        {
            if (listener.path == normalizedPath)
            {
                ids.push_back(listener.id);
            }
        }

        return ids;
    }

    // Get queue size
    size_t getQueueSize() const { return messageQueue.size(); }

    // Get listener count
    size_t getListenerCount() const { return listeners.size(); }

    // Get listener count for specific path
    int getListenerCount(const String &path) const
    {
        String normalizedPath = normalizePath(path);
        int count = 0;

        for (const Listener &listener : listeners)
        {
            if (listener.path == normalizedPath && listener.active)
            {
                count++;
            }
        }

        return count;
    }

    // Clear all listeners
    void clearListeners()
    {
        listeners.clear();
        Serial.println("All listeners cleared");
    }

    // Clear message queue
    void clearQueue()
    {
        messageQueue.clear();
        Serial.println("Message queue cleared");
    }

    // Clear all path locks
    void clearLocks()
    {
        pathLocks.clear();
        Serial.println("All path locks cleared");
    }

    // Debug: Print comprehensive status
    void printStatus() const
    {
        Serial.printf("=== Message Router Status ===\n");
        Serial.printf("Listeners: %d (active: %d)\n", listeners.size(), getActiveListenerCount());
        Serial.printf("Queue size: %d\n", messageQueue.size());
        Serial.printf("Path locks: %d\n", pathLocks.size());
        Serial.printf("Next listener ID: %u\n", nextListenerId);
        Serial.printf("Next message ID: %u\n", nextMessageId);
        Serial.printf("========================\n");

        // Print listeners
        for (const Listener &listener : listeners)
        {
            Serial.printf("Listener ID: %u, Path: %s, Owner: %s, Active: %s\n",
                         listener.id, listener.path.c_str(), listener.ownerID.c_str(),
                         listener.active ? "Yes" : "No");
        }

        // Print path locks
        for (const auto &pair : pathLocks)
        {
            const PathLock &lock = pair.second;
            Serial.printf("Lock: %s, Mode: %d, Sender: %s, Receiver: %s, Finalized: %s\n",
                         lock.path.c_str(), (int)lock.mode, 
                         lock.senderID.c_str(), lock.receiverID.c_str(),
                         lock.finalized ? "Yes" : "No");
        }
    }

private:
    int getActiveListenerCount() const
    {
        int count = 0;
        for (const Listener &listener : listeners)
        {
            if (listener.active) count++;
        }
        return count;
    }
};

// Enhanced example usage
class MessageRouterExample
{
private:
    MessageRouter router;
    uint32_t tempListenerId;
    uint32_t sensorListenerId;

public:
    void setup()
    {
        Serial.begin(115200);
        Serial.println("Enhanced Message Router Example Starting...");

        // Add listeners with IDs and owners
        uint32_t rootId = router.addListener("/", 
            [](const MessageRouter::Message &msg, const String &path, const String &senderID) {
                Serial.printf("[ROOT] From %s: %s -> %s\n", 
                             senderID.c_str(), msg.path.c_str(), msg.data.c_str());
            }, "system");

        sensorListenerId = router.addListener("/sensors", 
            [](const MessageRouter::Message &msg, const String &path, const String &senderID) {
                Serial.printf("[SENSORS] From %s: %s -> %s\n", 
                             senderID.c_str(), msg.path.c_str(), msg.data.c_str());
            }, "sensor-manager");

        tempListenerId = router.addListener("/sensors/temperature", 
            [](const MessageRouter::Message &msg, const String &path, const String &senderID) {
                Serial.printf("[TEMP] From %s: %s -> %s (ID: %u, Time: %lu)\n",
                             senderID.c_str(), msg.path.c_str(), msg.data.c_str(), 
                             msg.messageId, msg.timestamp);
            }, "temp-sensor");

        // Demonstrate path locking
        router.lockPath("/sensors/temperature", MessageRouter::LockMode::STRICT, 
                       "temp-sensor", "temp-device-01", String(tempListenerId));

        router.printStatus();
    }

    void loop()
    {
        static unsigned long lastMessage = 0;
        static int messageCounter = 0;

        router.processQueue();

        if (millis() - lastMessage > 4000)
        {
            lastMessage = millis();
            messageCounter++;

            switch (messageCounter % 6)
            {
            case 0:
                Serial.println("\n--- Sending from temp-device-01 to specific listener ---");
                router.sendMessage("/sensors/temperature", "25.6°C", 
                                 "temp-device-01", String(tempListenerId));
                break;

            case 1:
                Serial.println("\n--- Sending from unauthorized sender (should be blocked) ---");
                router.sendMessage("/sensors/temperature", "HACK ATTEMPT", 
                                 "malicious-device", String(tempListenerId));
                break;

            case 2:
                Serial.println("\n--- Sending to sensors (bubbling test) ---");
                router.sendMessage("/sensors", "All sensors OK", "sensor-manager");
                break;

            case 3:
                Serial.println("\n--- Queuing high priority message ---");
                router.queueMessage("/sensors/alert", "HIGH TEMP!", "temp-device-01", "", 10);
                break;

            case 4:
                Serial.println("\n--- Testing listener deactivation ---");
                router.setListenerActive(sensorListenerId, false);
                router.sendMessage("/sensors", "This should only reach root", "test");
                router.setListenerActive(sensorListenerId, true);
                break;

            case 5:
                Serial.println("\n--- Listener management test ---");
                std::vector<uint32_t> ids = router.getListenerIds("/sensors");
                Serial.printf("Found %d listeners on /sensors: ", ids.size());
                for (uint32_t id : ids) {
                    Serial.printf("%u ", id);
                }
                Serial.println();
                break;
            }

            Serial.printf("Queue size: %d\n", router.getQueueSize());
        }
    }

    void demonstrateLocking()
    {
        Serial.println("\n=== Path Locking Demonstration ===");
        
        // Show lock status
        MessageRouter::PathLock* lock = router.getLockStatus("/sensors/temperature");
        if (lock) {
            Serial.printf("Path /sensors/temperature is locked (mode: %d)\n", (int)lock->mode);
        }

        // Try to unlock with wrong owner (should fail)
        bool result = router.unlockPath("/sensors/temperature", "wrong-owner");
        Serial.printf("Unlock attempt by wrong owner: %s\n", result ? "SUCCESS" : "FAILED");

        // Unlock with correct owner
        result = router.unlockPath("/sensors/temperature", "temp-sensor");
        Serial.printf("Unlock attempt by correct owner: %s\n", result ? "SUCCESS" : "FAILED");
    }
};

#endif // MESSAGE_ROUTER_H