import numpy as np
import math
import sys
import os
np.set_printoptions(threshold=sys.maxsize)

with open(os.path.join(os.path.dirname(__file__), "link.traf"), "r") as f:
    interval = int(f.readline())

    while f.readline() != "\n":
        pass

    col = int(f.readline())
    row = 28
    link_traf_bytes = np.zeros((row, col), float)

    while f.readline() != "\n":
        pass

    i = 0
    j = 0

    for line in f:
        if line != "\n":
            link_traf_bytes[i][j] = int(line)
            i += 1
        else:
            j += 1
            i = 0

with open(os.path.join(os.path.dirname(__file__), "link.pktc"), "r") as f:
    link_pktc = np.zeros((row, col), float)

    i = 0
    j = 0

    for line in f:
        if line != "\n":
            link_pktc[i][j] = int(line)
            i += 1
        else:
            j += 1
            i = 0

with open(os.path.join(os.path.dirname(__file__), "link.loss"), "r") as f:
    link_loss = np.zeros((row, col), float)

    i = 0
    j = 0

    for line in f:
        if line != "\n":
            link_loss[i][j] = int(line)
            i += 1
        else:
            j += 1
            i = 0

link_traf = link_traf_bytes * 8 / interval / 1000000
link_no_loss = link_pktc + link_loss
link_loss_rate = np.divide(link_loss, link_no_loss, out=np.zeros_like(
    link_loss), where=link_no_loss != 0)

i = 1
print("linktraffic\n", link_traf[:, i])
# print("linktraffic*1.2(rounded)\n", np.round(link_traf[:, i]*1.2,0))
print("linklossrate\n", link_loss_rate[:, i])