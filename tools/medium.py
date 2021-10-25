# tagger juste le premier
# Fenetre source -> dest
# 2 modes drop / ecn / mode hybride


import socket
import select
import sys
import random
import getopt
import time

ECN_ACTIVE = 1
ECN_DISABLED = 0

TYPE_SYN = 1
TYPE_FIN = 2
TYPE_ACK = 16
TYPE_RST = 4

MAX_PACKETS = 100


def help():
    print("Usage : python3 medium.py [option]\n\n" \
          "This program is used to simulate loss of congestion (with the ECN mode). In normal mode, generates 30 percents loss when receiving more than 100 packets within a second. In ECN mode, set the ECN bit for the first packet of a time interval of 1 second when receiving more than 100 packets within a second.\n\n" \
          "\t-v,--verbose\t\tUsed for debug, display the pseudo TCP header for each received packet\n" \
          "\t-s,--second\t\tDisplay the number of received message each second\n" \
          "\t-e,--ecn\t\tActivate the ECN mode\n" \
          "\t-l,--limit [val]\tSet the packet limit rate before loss to the choosen value\n")


def parse_type(t):
    ok_ = False
    print("Type :", end="")
    if t & TYPE_FIN:
        print(" FIN ", end="")
        ok_ = True
    if t & TYPE_ACK:
        print(" ACK ", end="")
        ok_ = True
    if t & TYPE_SYN:
        print(" SYN ", end="")
        ok_ = True
    if t & TYPE_RST:
        print(" RST ", end="")
        ok_ = True
    if not ok_:
        print("Unrecognized Type : Please check your packet format", end="")
    print("")


def parse_ecn(e):
    if e >= ECN_ACTIVE:
        print("ECN bit is enabled")
    elif e == ECN_DISABLED:
        print("ECN bit is disable")
    else:
        print("There is an issue with your ECN bit : please check your packet format")


##############################################################
## Packet format : Type, Seq, Ecn, Payload ###################
##############################################################

def parse_new_messages(msg):
    print("\nNew source message received")
    flux = msg[0]
    print("Flux ID : {}".format(flux))
    t = msg[1]
    parse_type(t)
    nchars = 1
    #   string to int or long. Type depends on nchars
    seq = sum((msg[byte + 2]) << 8 * (nchars - byte - 1) for byte in range(nchars))
    print("SeqNum : {}".format(seq))
    nchars = 1
    #   string to int or long. Type depends on nchars
    ack_seq = sum((msg[byte + 4]) << 8 * (nchars - byte - 1) for byte in range(nchars))
    print("Ack seq Num : {}".format(ack_seq))
    # seq = ord(msg[1:4])
    ecn = msg[6]
    parse_ecn(ecn)
    win_size = msg[7]
    print("Size of congestion Window : {}".format(win_size))


def parse_new_messages_server(msg):
    print("\nNew server message received")
    flux = msg[0]
    print("Flux ID : {}".format(flux))
    t = msg[1]
    parse_type(t)
    nchars = 1
    #   string to int or long. Type depends on nchars
    seq = sum((msg[byte + 2]) << 8 * (nchars - byte - 1) for byte in range(nchars))
    print("SeqNum : {}".format(seq))
    nchars = 1
    #   string to int or long. Type depends on nchars
    ack_seq = sum((msg[byte + 4]) << 8 * (nchars - byte - 1) for byte in range(nchars))
    print("Ack seq Num : {}".format(ack_seq))
    # seq = ord(msg[1:4])
    ecn = msg[6]
    parse_ecn(ecn)
    win_size = msg[7]
    print("Size of new Window : {}".format(win_size))


debug = False
verb = False
second = False
ecn = False
hybride = False

options, remainder = getopt.getopt(sys.argv[1:], 'dvsehl:', ['debug', 'verbose', 'second', 'ecn', 'help', 'limit=', ])

for opt, arg in options:
    if opt in ('-d', '--debug'):
        debug = True
    if opt in ('-v', '--verbose'):
        verb = True
    if opt in ('-s', '--second'):
        second = True
    if opt in ('-e', '--ecn'):
        ecn = True
    if opt in ('-l', '--limit'):
        MAX_PACKETS = arg
    if opt in ('-h', '--help'):
        help()
        exit(0)

random.seed(time.time())

sock_sender = socket.socket(socket.AF_INET, socket.SOCK_DGRAM)
sock_recv = socket.socket(socket.AF_INET, socket.SOCK_DGRAM)

dest_sender = "127.0.0.1"
dest_recv = "127.0.0.1"
port_sender = 3333
port_recv = 6666

sock_sender.bind(("127.0.0.1", 4444))
sock_recv.bind(("127.0.0.1", 5555))


input = [sock_sender, sock_recv, sys.stdin]

con = True
act_time = time.time()
nb_packets = 0
tagged = False

while con:
    if act_time + 1 <= time.time():
        if second:
            print("{} packets received last second".format(nb_packets))
            nb_packets = 0
            act_time = time.time()
        tagged = False

    i_ready, o_ready, e_ready = select.select(input, [], [], 1.0)

    for s in i_ready:
        if s == sock_sender:
            data, addr = sock_sender.recvfrom(64)
            nb_packets += 1
            if nb_packets > MAX_PACKETS:
                if ecn:
                    if not tagged:
                        data_tmp = bytearray(data)
                        data_tmp[5] = ECN_ACTIVE
                        data = bytes(data_tmp)
                        tagged = True
                    sock_sender.sendto(data, (dest_recv, port_recv))
                else:
                    if random.random() <= 0.7:
                        sock_sender.sendto(data, (dest_recv, port_recv))
            else:
                sock_sender.sendto(data, (dest_recv, port_recv))

            if debug:
                print("message {} received from sender".format(data))
            if verb:
                parse_new_messages(data)

        elif s == sock_recv:
            data, addr = sock_recv.recvfrom(64)
            sock_recv.sendto(data, (dest_sender, port_sender))
            if debug:
                print("message {} received from receiver".format(data))
                # parse_new_messages(data)
            if verb:
                parse_new_messages_server(data)
        elif s == sys.stdin:
            data = sys.stdin.readline()
            data = data.strip()
            if data == "quit":
                con = False
        else:
            print("WTF ?!?")

sock_recv.close()
sock_sender.close()
