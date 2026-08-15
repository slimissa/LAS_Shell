#!/usr/bin/env python3
"""
chaos_proxy.py — Las_shell Milestone 2: local TCP fault-injection proxy
==========================================================================
Sits between las_shell (BROKER_API pointed here) and an upstream broker
endpoint (sim_server.py locally, or a real paper-trading API when run on
a machine with network access to one). Relays bytes transparently in
--mode passthrough; in the other modes it deliberately breaks the
connection in one specific, realistic way so broker.c's timeout/error
handling can be exercised without needing a real broker outage to happen
on cue.

Modes
-----
  passthrough  Forward bytes unmodified both directions (default).
  stall        Accept the connection, read the request, then never
               write a response. Client-side curl (CURLOPT_TIMEOUT=10s
               in broker.c) eventually times out on its own — this mode
               doesn't need to do anything except NOT respond.
  drop         Accept the connection, read the request, then close
               immediately without writing anything (simulates a reset
               connection / broker-side crash mid-request).
  truncate     Forward the request, start relaying the real response,
               then cut the connection after N bytes (simulates a
               response that starts but never finishes).

Usage
    python3 chaos_proxy.py --listen-port 18090 \\
        --upstream-host localhost --upstream-port 18080 \\
        --mode stall
"""
import argparse
import socket
import threading
import sys


def handle_passthrough(client_sock, upstream_host, upstream_port):
    try:
        upstream = socket.create_connection((upstream_host, upstream_port), timeout=5)
    except OSError as e:
        client_sock.close()
        print(f"[chaos_proxy] upstream connect failed: {e}", file=sys.stderr)
        return

    def relay(src, dst):
        try:
            while True:
                data = src.recv(4096)
                if not data:
                    break
                dst.sendall(data)
        except OSError:
            pass
        finally:
            try:
                dst.shutdown(socket.SHUT_WR)
            except OSError:
                pass

    t1 = threading.Thread(target=relay, args=(client_sock, upstream), daemon=True)
    t2 = threading.Thread(target=relay, args=(upstream, client_sock), daemon=True)
    t1.start(); t2.start()
    t1.join(); t2.join()
    client_sock.close(); upstream.close()


def handle_stall(client_sock, *_):
    # Read whatever the client sends (so it's not left blocked on write
    # buffer backpressure), then simply never respond. broker.c's own
    # CURLOPT_TIMEOUT is what ends this, not us.
    try:
        client_sock.settimeout(30)
        client_sock.recv(65536)
    except OSError:
        pass
    # Deliberately no response, no close — hold the connection open until
    # the client (curl) gives up on its own timeout.
    try:
        client_sock.settimeout(None)
        while True:
            data = client_sock.recv(4096)
            if not data:
                break
    except OSError:
        pass
    finally:
        client_sock.close()


def handle_drop(client_sock, *_):
    try:
        client_sock.settimeout(5)
        client_sock.recv(65536)
    except OSError:
        pass
    # Reset rather than a clean close, closer to a real crashed-server
    # symptom (RST) than a graceful FIN.
    try:
        client_sock.setsockopt(socket.SOL_SOCKET, socket.SO_LINGER,
                                bytes([1, 0, 0, 0]))
    except OSError:
        pass
    client_sock.close()


def handle_truncate(client_sock, upstream_host, upstream_port, truncate_bytes):
    try:
        upstream = socket.create_connection((upstream_host, upstream_port), timeout=5)
    except OSError as e:
        client_sock.close()
        print(f"[chaos_proxy] upstream connect failed: {e}", file=sys.stderr)
        return
    try:
        request = client_sock.recv(65536)
        upstream.sendall(request)
        sent = 0
        while sent < truncate_bytes:
            chunk = upstream.recv(min(4096, truncate_bytes - sent))
            if not chunk:
                break
            client_sock.sendall(chunk)
            sent += len(chunk)
    except OSError:
        pass
    finally:
        # Close without sending the rest — simulates a response that
        # started but never completed.
        client_sock.close()
        upstream.close()


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--listen-port", type=int, required=True)
    ap.add_argument("--upstream-host", default="localhost")
    ap.add_argument("--upstream-port", type=int, required=True)
    ap.add_argument("--mode", choices=["passthrough", "stall", "drop", "truncate"],
                     default="passthrough")
    ap.add_argument("--truncate-bytes", type=int, default=20)
    args = ap.parse_args()

    srv = socket.socket(socket.AF_INET, socket.SOCK_STREAM)
    srv.setsockopt(socket.SOL_SOCKET, socket.SO_REUSEADDR, 1)
    srv.bind(("127.0.0.1", args.listen_port))
    srv.listen(8)
    print(f"[chaos_proxy] mode={args.mode} listening on 127.0.0.1:{args.listen_port} "
          f"-> {args.upstream_host}:{args.upstream_port}", file=sys.stderr)

    while True:
        client_sock, _addr = srv.accept()
        if args.mode == "passthrough":
            t = threading.Thread(target=handle_passthrough,
                                  args=(client_sock, args.upstream_host, args.upstream_port),
                                  daemon=True)
        elif args.mode == "stall":
            t = threading.Thread(target=handle_stall, args=(client_sock,), daemon=True)
        elif args.mode == "drop":
            t = threading.Thread(target=handle_drop, args=(client_sock,), daemon=True)
        else:
            t = threading.Thread(target=handle_truncate,
                                  args=(client_sock, args.upstream_host, args.upstream_port,
                                        args.truncate_bytes),
                                  daemon=True)
        t.start()


if __name__ == "__main__":
    main()
