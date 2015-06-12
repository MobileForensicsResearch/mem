# mem
Tool used for dumping memory from Android devices. Root access is required.

./mem pid out_path
 - where pid is the target PID to capture
 - and out_path is the local dir to write output
 If out_path is not there, writes to stdout

To ensure forensic soundness, mem should be copied into memory (/dev or another tmpfs location), and netcat should be used to write data out over ADB to avoid writing to the device. Netcat versions compiled for Android can be found at https://github.com/MobileForensicsResearch/netcat

Eg:

1: On local machine run:
adb forward tcp:9999 tcp:9999

2: From adb shell run:
./mem pid | nc -l -p 9999

3: On local machine run:
nc 127.0.0.1 9999 > output_file