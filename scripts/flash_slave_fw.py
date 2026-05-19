Import("env")
import subprocess
import os


def flash_slave_fw(source, target, env):
    fw = os.path.join(env["PROJECT_DIR"], "c6_slave_fw", "network_adapter.bin")
    if not os.path.exists(fw):
        print("WARNING: c6_slave_fw/network_adapter.bin not found — skipping C6 flash")
        return

    port = env.subst("$UPLOAD_PORT")
    if not port:
        print("WARNING: no upload port — skipping C6 flash")
        return

    # Use the esptool.py bundled with PlatformIO packages (avoids Python env issues)
    python_exe = env.subst("$PYTHONEXE")
    uploader = env.subst("$UPLOADER")  # e.g. ~/.platformio/packages/tool-esptool-py/esptool.py

    if uploader and os.path.exists(uploader):
        cmd = [python_exe, uploader]
    else:
        # fallback: try esptool from PlatformIO packages directly
        pio_esptool = os.path.expanduser("~/.platformio/packages/tool-esptool-py/esptool.py")
        if os.path.exists(pio_esptool):
            cmd = [python_exe, pio_esptool]
        else:
            print("WARNING: esptool not found — skipping C6 flash")
            return

    cmd += [
        "--chip",
        "esp32p4",
        "--port",
        port,
        "-b",
        "460800",
        "write_flash",
        "0x10000",  # slave_fw partition offset
        fw,
    ]
    print(f"Flashing C6 slave firmware to slave_fw partition ({fw})...")
    subprocess.run(cmd, check=True)


env.AddPostAction("upload", flash_slave_fw)
