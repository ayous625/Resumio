# Resumio 🛡️👀

**Resumio** is a lightweight C++ Windows system tray utility designed to protect your privacy. It tracks real-time webcam and microphone usage through the Windows Registry (`ConsentStore`) and alerts you if an active device-using app is configured to launch automatically at startup[cite: 1].

---

## ✨ Features

* **Real-Time Monitoring:** Continuously tracks active webcam and microphone sessions via the Windows Registry[cite: 1].
* **Startup Risk Detection:** Automatically checks system and user startup keys (`Run`, `RunOnce`) to flag persistent background device usage[cite: 1].
* **System Tray Integration:** Runs silently with a dynamic tray icon showing current status (Idle, Active, or Warning)[cite: 1].
* **Interactive Notifications:** 
  * Displays desktop balloon alerts when your camera or microphone activates[cite: 1].
  * Warns you if an active app is registered as a startup program[cite: 1].
  * Provides a detailed status popup with timestamps and process paths upon double-click[cite: 1].

---

## 🛠️ Tech Stack & Requirements

* **Language:** C++20
* **Platform:** Windows 10 / 11 (`WindowsTargetPlatformVersion` 10.0)[cite: 1, 2]
* **Build Tools:** Microsoft Visual Studio (`Resumio.sln` included)[cite: 2, 4]
* **APIs Used:** Win32 API, Shell32, Advapi32, User32, GDI32[cite: 1]
