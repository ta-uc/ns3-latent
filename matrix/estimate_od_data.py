import numpy as np
import math
import sys
import os
from get_routing_matrix import get_routing_matrix


np.set_printoptions(threshold=sys.maxsize)
np.set_printoptions(suppress=True)

routes = ???
routing_matrix = get_routing_matrix(routes)
route = np.array(routing_matrix)
route = route.T
route_pinv = np.linalg.pinv(route)

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

with open(os.path.join(os.path.dirname(__file__), "capas_default"), "r") as f:
    capas_d = [float(line.replace("\n","").replace("Mbps","")) for line in f]

capas_d = np.array(capas_d)

link_traf = link_traf_bytes * 8 / interval / 1000000
link_no_loss = link_pktc + link_loss
link_loss_rate = np.divide(link_loss, link_no_loss, out=np.zeros_like(link_loss), where=link_no_loss != 0)
link_loss_rate_log = np.log(1-link_loss_rate)

od_flow = np.zeros((110, col), float)
od_flow = np.zeros((110, col), float)
od_loss_rate_log = np.zeros((110, col), float)
od_loss_rate = np.zeros((110, col), float)
od_latent = np.zeros((110, col), float)
link_latent = np.zeros((28, col), float)

for c in range(col):
    od_flow[:, c] = np.dot(route_pinv, link_traf[:, c])
    od_loss_rate_log[:, c] = np.dot(route.T, link_loss_rate_log[:, c])
    od_loss_rate[:, c] = (-1 * np.exp(od_loss_rate_log[:, c])) + 1
    for i in range(110):
        od_latent[:, c][i] = (1 / math.exp(-13.1 * od_loss_rate[:, c][i])) * od_flow[:, c][i]
    link_latent[:, c] = np.dot(route, od_latent[:, c])

with open("./matrix/capas_sep", "a") as f:
    for i in range(28):
        if link_traf[:,1][i] * (1 / math.exp(-13.1 * link_loss_rate[:, 1][i])) > int(capas_d[i]):
            print(f"{link_traf[:,1][i] * (1 / math.exp(-13.1 * link_loss_rate[:, 1][i]))}Mbps", file=f)
        else:
            print(f"{capas_d[i]}Mbps", file=f)
   
with open("./matrix/capas_od", "a") as f2:
    for c,oc in zip(link_latent[:,1],capas_d):
        if c > int(oc):
            print(f"{c}Mbps", file=f2)
        else:
            print(f"{oc}Mbps", file=f2)