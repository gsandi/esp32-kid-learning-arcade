Import("env")
import subprocess
import os


def flash_slave_fw(source, target, env):
    fw = os.path.join(env["PROJECT_DIR"], "c6_slave_fw", "network_adapter.bin")
    if not os.path.exists(fw):
        print("WARNING: c6_slave_fw/network_adapter.bin not found — skipping C6 flash")
        return
    port = env.get("UPLOAD_PORT", env.get("MONITOR_PORT", ""))
    if not port:
        print("WARNING: no upload port — skipping C6 flash")
        return
    cmd = [
        env.get("PYTHONEXE", "python"),
        "-m",
        "esptool",
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
    print("Flashing C6 slave firmware to partition offset 0x10000...")
    subprocess.run(cmd, check=True)


env.AddPostAction("upload", flash_slave_fw)
