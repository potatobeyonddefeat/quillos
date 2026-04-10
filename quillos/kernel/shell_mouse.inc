    else if (safe_compare(cmd, "mouseinfo")) {
        char buf[32];
        if (!Mouse::is_present()) {
            kprint("\nMouse: not detected");
        } else {
            kprint("\nMouse:");
            kprint("\n  X:       "); itoa(Mouse::get_x(), buf); kprint(buf);
            kprint("\n  Y:       "); itoa(Mouse::get_y(), buf); kprint(buf);
            uint8_t b = Mouse::get_buttons();
            kprint("\n  Buttons: ");
            if (b & Mouse::BTN_LEFT)   kprint("LEFT ");
            if (b & Mouse::BTN_RIGHT)  kprint("RIGHT ");
            if (b & Mouse::BTN_MIDDLE) kprint("MIDDLE ");
            if (b == 0) kprint("(none)");
            kprint("\n  Packets: "); itoa(Mouse::get_packet_count(), buf); kprint(buf);
        }
    }
    else if (safe_compare(cmd, "copy")) {
        if (safe_strlen(arg) == 0) {
            kprint("\nUsage: copy <text>");
        } else {
            Clipboard::set_str(arg);
            char buf[16];
            kprint("\nCopied "); itoa(Clipboard::size(), buf); kprint(buf);
            kprint(" bytes to clipboard");
        }
    }
    else if (safe_compare(cmd, "copyf")) {
        if (safe_strlen(arg) == 0) {
            kprint("\nUsage: copyf <file>");
        } else {
            char path[QuillFS::MAX_PATH_LEN] = {0};
            build_path(arg, path);
            char content[QuillFS::MAX_FILE_DATA] = {0};
            if (g_fs.read_file(path, content, sizeof(content))) {
                Clipboard::set_str(content);
                char buf[16];
                kprint("\nCopied "); itoa(Clipboard::size(), buf); kprint(buf);
                kprint(" bytes from "); kprint(arg);
            } else {
                kprint("\nCannot read: "); kprint(arg);
            }
        }
    }
    else if (safe_compare(cmd, "paste")) {
        if (Clipboard::empty()) {
            kprint("\n(clipboard empty)");
        } else {
            kprint("\n");
            kprint(Clipboard::get());
        }
    }
    else if (safe_compare(cmd, "pastef")) {
        if (safe_strlen(arg) == 0) {
            kprint("\nUsage: pastef <file>");
        } else if (Clipboard::empty()) {
            kprint("\nClipboard is empty");
        } else {
            char path[QuillFS::MAX_PATH_LEN] = {0};
            build_path(arg, path);
            if (g_fs.write_file(path, Clipboard::get())) {
                char buf[16];
                kprint("\nWrote "); itoa(Clipboard::size(), buf); kprint(buf);
                kprint(" bytes to "); kprint(arg);
            } else {
                kprint("\nCannot write: "); kprint(arg);
            }
        }
    }
    else if (safe_compare(cmd, "cut")) {
        if (safe_strlen(arg) == 0) {
            kprint("\nUsage: cut <file>");
        } else {
            char path[QuillFS::MAX_PATH_LEN] = {0};
            build_path(arg, path);
            char content[QuillFS::MAX_FILE_DATA] = {0};
            if (g_fs.read_file(path, content, sizeof(content))) {
                Clipboard::set_str(content);
                g_fs.rm(path);
                char buf[16];
                kprint("\nCut "); itoa(Clipboard::size(), buf); kprint(buf);
                kprint(" bytes from "); kprint(arg);
                kprint(" (file removed)");
            } else {
                kprint("\nCannot cut: "); kprint(arg);
            }
        }
    }
    else if (safe_compare(cmd, "pasteh")) {
        kprint("\nReading from host terminal (type text there, press Enter; 10s timeout)...");
        char hbuf[512];
        uint32_t n = Serial::read_line(hbuf, sizeof(hbuf), 10000);
        if (n == 0) {
            kprint("\n(nothing received)");
        } else {
            Clipboard::set(hbuf, n);
            char buf[16];
            kprint("\nReceived "); itoa(n, buf); kprint(buf);
            kprint(" bytes: "); kprint(hbuf);
            kprint("\n(now in clipboard)");
        }
    }
