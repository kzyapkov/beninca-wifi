#!/usr/bin/env python2

import sys
import os
import os.path as p
import logging
import argparse
from datetime import datetime
from logging.handlers import TimedRotatingFileHandler, SocketHandler

import serial

_logdir = p.realpath('logs')

parser = argparse.ArgumentParser(
    formatter_class=argparse.ArgumentDefaultsHelpFormatter)
parser.add_argument('--port', default=os.environ.get('MOS_PORT', '/dev/ttyUSB0'),
                    help="Which serial port to use")
parser.add_argument('--baud', default=115200,
                    help="BAUD rate for the serial port")
parser.add_argument('--log-dir', nargs="?", default=_logdir,
                    help="Where to store log files")
parser.add_argument('--log-addr', nargs="?",
                    help="Where to netcat log data, host:port")
parser.add_argument('-q', '--quiet', default=False,
                    help="Disable HAC log on stdout")


def applog(quiet=False):
    log = logging.getLogger('hacvisor')
    log.setLevel(logging.DEBUG)
    hndlr = logging.StreamHandler(sys.stdout)
    hndlr.setLevel(logging.INFO if quiet else logging.DEBUG)
    hndlr.setFormatter(logging.Formatter(
        u"%(asctime)s %(levelname)-5.5s %(message)s"))
    log.addHandler(hndlr)
    return log


def shelog(log_dir=None, log_addr=(None, None)):
    log = logging.getLogger('she')
    log.setLevel(logging.DEBUG)
    fmtr = logging.Formatter(u"%(asctime)s %(message)s")

    if log_dir:
        hndlr = TimedRotatingFileHandler(p.join(log_dir, 'serial.log'),
            when="midnight", interval=1, backupCount=14, encoding="utf8")
        hndlr.setLevel(logging.DEBUG)
        hndlr.setFormatter(fmtr)
        log.addHandler(hndlr)

    if log_addr[0]:
        hndlr = SocketHandler(*log_addr)
        hndlr.setLevel(logging.DEBUG)
        hndlr.setFormatter(fmtr)
        log.addHandler(hndlr)

    return log

class Backtracer(object):

    START_DELIM = '--- BEGIN CORE DUMP ---'
    END_DELIM =   '---- END CORE DUMP ----'
    LOGBUF_LINES = 300

    def __init__(self, dir_, prefix):
        self.dir = p.realpath(dir_)
        self.prefix = prefix
        self.logbuf = []
        self.backtrace = None
        self.created = None

    def add(self, line):
        if not self.backtrace:
            if line.find(self.START_DELIM) != -1:
                self.backtrace = [line]
                self.created = datetime.now()
                return

            self.logbuf.append(line)
            while len(self.logbuf) > self.LOGBUF_LINES:
                self.logbuf.pop(0)

        else:
            self.backtrace.append(line)
            if line.find(self.END_DELIM) != -1:
                self.emit()
                self.backtrace = None
                self.created = None
                self.logbuf = []

    def emit(self):
        fn = "{}_{}.coredump".format(
                self.prefix, self.created.strftime("%Y%m%d-%H%M"))

        with open(p.join(self.dir, fn), 'w') as fp:
            for line in self.logbuf + self.backtrace:
                fp.write(line)
                if not line.endswith('\n'): fp.write('\n')

def main():
    args = parser.parse_args()
    print("Starting with: %s" % (args,))

    log = applog(args.quiet)

    if args.log_dir:
        if not os.path.exists(args.log_dir):
            try:
                os.mkdir(args.log_dir)
            except (OSError, IOError):
                log.error("%s did not exist and we could not create it")

        if not os.path.isdir(args.log_dir) or not os.access(args.log_dir, os.W_OK):
            log.error("%s: not a directory or not writable", args.log_dir)
            sys.exit(1)
        log.info("Log files will be stored in %s", args.log_dir)

    (host, port) = (None, None)
    if args.log_addr:
        try:
            host, port = args.log_addr.split(':')
            port = int(port)
            log.info("Log records will be sent via TCP to %s:%d", host, port)
        except (TypeError, ValueError) as e:
            log.error("Invalid --log-addr='%s': %s", args.log_addr, e)
            sys.exit(3)

    she = shelog(args.log_dir, (host, port))
    bt = Backtracer(args.log_dir, '')
    try:
        port = serial.Serial(args.port, baudrate=args.baud, timeout=0.2)
    except Exception as e:
        log.error("Unable to open serial port %s: %s", args.port, e)
        sys.exit(2)

    try:
        while True:
            line = port.readline()
            if not len(line):
                continue

            bt.add(line)

            log.info("%s", line.rstrip("\r\n"))
            if line.endswith('\n'):
                she.info("| %s", line.rstrip("\r\n"))
            else:
                she.info("  %s", line.rstrip("\r\n"))

            [h.flush() for h in she.handlers]

    except KeyboardInterrupt:
        port.close()
        sys.exit(0)
    except Exception as e:
        log.exception("Unhandled mainloop error: %s", e)
        port.close()
        sys.exit(15)


if __name__ == '__main__':
    main()
