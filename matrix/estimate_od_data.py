import numpy as np
import math
import sys
import os
import argparse
import itertools
from topology import nodes

parser = argparse.ArgumentParser(description='situ: before, after, last')
parser.add_argument('--Situ', action="store", dest="situ", default="before")
params = parser.parse_args()

link_numbering = {
  "AB": 0, "AC": 1, "BA": 2, "BC": 3,
  "BD": 4, "CA": 5, "CB": 6, "CE": 7,
  "DB": 8, "DF": 9, "EC": 10,"EF": 11,
  "EH": 12,"FE":13, "FD": 14,"FI": 15,
  "GH": 16,"GK":17, "HE": 18,"HG": 19,
  "HI": 20,"IF":21, "IH": 22,"IJ": 23,
  "JI": 24,"JK":25, "KG": 26,"KJ": 27
}

def get_routing_matrix(routes):

  routing_matrix = []
  for route in routes:
    matrix_one_row = [0] * len(link_numbering)
    for od in route.keys():
      matrix_one_row[link_numbering[od[3:5]]] = route[od]
    routing_matrix.append(matrix_one_row)
  return routing_matrix

def estimate_od_data():

    if params.situ == "before":
        from orig_route import route_lists
    elif params.situ == "after":
        from util_opt_route import route_lists
    elif params.situ == "last":
        from util_capa_opt_route import route_lists

    np.set_printoptions(threshold=sys.maxsize)
    np.set_printoptions(suppress=True)

    routing_matrix = get_routing_matrix(route_lists)
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

    link_traf = link_traf_bytes * 8 / interval / 1000000
    link_no_loss = link_pktc + link_loss
    link_loss_rate = np.divide(link_loss, link_no_loss, out=np.zeros_like(link_loss), where=link_no_loss != 0)
    link_loss_rate_log = np.log(1-link_loss_rate)

    od_flow = np.zeros((110, col), float)
    od_flow = np.zeros((110, col), float)
    od_loss_rate_log = np.zeros((110, col), float)
    od_loss_rate = np.zeros((110, col), float)
    od_latent = np.zeros((110, col), float)
    # link_latent = np.zeros((28, col), float)

    if params.situ == "before":
        for c in range(col):
            od_flow[:, c] = np.dot(route_pinv, link_traf[:, c])
            od_loss_rate_log[:, c] = np.dot(route.T, link_loss_rate_log[:, c])
            od_loss_rate[:, c] = (-1 * np.exp(od_loss_rate_log[:, c])) + 1

        throughput_actual = {od[0]+od[1]: flow for od, flow in zip(itertools.permutations(nodes, 2), od_flow[:, 1])}
        loss = {od[0]+od[1]: loss_rate for od, loss_rate in zip(itertools.permutations(nodes, 2), od_loss_rate[:, 1])}
        
        with open("od_data_before.py", "w") as odrate_b_f:
            print(f"actual = {repr(throughput_actual)}", file=odrate_b_f)
            print(f"loss = {repr(loss)}", file=odrate_b_f)

    elif params.situ == "after":
        for c in range(col):
            od_flow[:, c] = np.dot(route_pinv, link_traf[:, c])
            od_loss_rate_log[:, c] = np.dot(route.T, link_loss_rate_log[:, c])
            od_loss_rate[:, c] = (-1 * np.exp(od_loss_rate_log[:, c])) + 1

        throughput_actual = {od[0]+od[1]: flow for od, flow in zip(itertools.permutations(nodes, 2), od_flow[:, 1])}
        loss = {od[0]+od[1]: loss_rate for od, loss_rate in zip(itertools.permutations(nodes, 2), od_loss_rate[:, 1])}
        
        with open("od_data_after.py", "w") as odrate_a_f:
            print(f"actual = {repr(throughput_actual)}", file=odrate_a_f)
            print(f"loss = {repr(loss)}", file=odrate_a_f)

        from estimate_gamma import estimate_gamma
        gamma = estimate_gamma()

        for i in range(110):
            od_latent[:, c][i] = (1 / math.exp(gamma * od_loss_rate[:, c][i])) * od_flow[:, c][i]
        
        throughput_latent = {od[0]+od[1]: flow for od, flow in zip(itertools.permutations(nodes, 2), od_latent[:, 1])}

        with open("od_data_after.py", "a") as odrate_a_f:
            print(f"latent = {repr(throughput_latent)}", file=odrate_a_f)  
    
    elif params.situ == "last":
        for c in range(col):
            od_flow[:, c] = np.dot(route_pinv, link_traf[:, c])
            od_loss_rate_log[:, c] = np.dot(route.T, link_loss_rate_log[:, c])
            od_loss_rate[:, c] = (-1 * np.exp(od_loss_rate_log[:, c])) + 1

        throughput_actual = {od[0]+od[1]: flow for od, flow in zip(itertools.permutations(nodes, 2), od_flow[:, 1])}
        loss = {od[0]+od[1]: loss_rate for od, loss_rate in zip(itertools.permutations(nodes, 2), od_loss_rate[:, 1])}
        
        with open("od_data_last.py", "w") as odrate_l_f:
            print(f"actual = {repr(throughput_actual)}", file=odrate_l_f)
            print(f"loss = {repr(loss)}", file=odrate_l_f)

        for i in range(110):
            od_latent[:, c][i] = (1 / math.exp(-13.1 * od_loss_rate[:, c][i])) * od_flow[:, c][i]
        
        throughput_latent = {od[0]+od[1]: flow for od, flow in zip(itertools.permutations(nodes, 2), od_latent[:, 1])}

        with open("od_data_last.py", "a") as odrate_l_f:
            print(f"latent = {repr(throughput_latent)}", file=odrate_l_f)

estimate_od_data()