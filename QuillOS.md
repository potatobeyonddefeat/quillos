# QuillOS

**QuillOS** is a privacy-focused operating system designed for developers.  
Its core philosophy is simple:

> **Deployment should be quick and secure.**

QuillOS aims to minimize friction between writing code and running it, while maintaining strong security defaults and respecting user privacy.

---

# Philosophy

QuillOS is built around three guiding principles:

1. **Fast Deployment**
   - Developers should be able to go from idea → running service in seconds.

2. **Secure by Default**
   - The system should protect users without requiring manual configuration.

3. **Developer First**
   - Built-in tooling for programming, local servers, and project management.

---

# Core Goals

- Privacy-focused system (no telemetry)
- Fast developer environment setup
- Simple local server deployment
- Secure system defaults
- Minimal system overhead
- Flexible CLI + GUI workflow

---

# System Architecture

Hardware
↓
BIOS
↓
Limine Bootloader
↓
Linux Kernel (Monolithic)
↓
QuillInit (startup system)
↓
Bash Shell
↓
Quill Tools + qpkg
↓
CLI or GUI

---

# Core Components

| Component | Description |
|--------|--------|
| **Limine** | Bootloader |
| **Linux Kernel** | Hardware compatibility and core OS functionality |
| **QuillInit** | System initialization manager |
| **Bash** | Default shell |
| **qpkg** | Package manager |
| **quill CLI** | Developer-focused system tool |
| **QuillDesk** | Optional graphical desktop environment |

---

# Package Manager

QuillOS uses **qpkg** as its package manager.

Example usage:

```bash
qpkg install node
qpkg remove node
qpkg update
qpkg search docker