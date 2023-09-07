# RDP-Replay-Docker

It is a simple fork of [RDP-Replay](https://github.com/ctxis/RDP-Replay) dockerized.

# Build

```bash
$ git clone https://github.com/blackndoor/RDP-Replay-Docker.git
$ cd RDP-Replay-Docker
$ docker build -t rdp-replay .
```

# Run

Show the help menu:

```bash
$ ./rdp-replay.sh -h
Usage: rdp_replay  <options>
    -h                    Help. You're reading it!
    -l <lsa_secrets_file> File containing LSA secrets for RDP decryption
    -L <lsa_raw_secret>   File containing a single binary LSA secret
    -o <output_file>      Output video file (e.g. "rdp.avi")
    -p <rsa_priv_file>    PEM file with SSL key (can be repeated)
    -r <pcap_file>        The pcap file (default is stdin)
    -t <port>             The TCP port to select in the pcap (default: any)
    -x <num>              Playback tcp stream at <num> times realtime
    --clipboard_16le      Clipboard is assumed to be UTF16le and stripped back up 8-bit
    --debug_chan          Show channel messages
    --debug_caps          Show capabilities messages
    --fullspeed           Playback tcp stream at full-speed
    --help                Help. You're still reading it!
    --no_cksum            Don't check the packet (IP and TCP) checksums
    --no_cursor           Don't show the cursor
    --realtime            Playback tcp stream in realtime
    --reverse             Reverse client/server direction (sometimes useful for extracted data)
    --save_clipboard      Save clipboard events to file (e.g. "clip-00000000-up")
    --show_time           Display packet capture time
    --show_keys           Display keypress (repeat for verbose)
    --sound               Play sounds (experimental)
    --rdprd               Display RDPDR channel requests
    --sw                  Use SW_GDI for rendering (not recommended)
```

Basic exemple:

```bash
$ ./rdp-replay.sh -r capture.pcap -p certificate.pem -t 49209 --no_cksum --show_keys -o video.avi --save_clipboard
```

# Thanks

* [ctxis](https://github.com/ctxis)
* [FBoisson](https://github.com/FBoisson)
