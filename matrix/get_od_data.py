from topology import nodes
import itertools
import os
import math
import argparse

parser = argparse.ArgumentParser(description='situ: before, after, last')
parser.add_argument('--Situ', action="store", dest="situ", default="before")
params = parser.parse_args()

through_act = {}
through_lat = {}
loss = {}

for nodeA, nodeB in itertools.permutations(nodes, 2):
    for port in range(10):
        thrsum = 0
        with open(os.path.join(os.path.dirname(__file__), f"Data/n{nodes.index(nodeA)}-n{nodes.index(nodeB)}-p{port+1}.thr"), "r") as f:
            for i in range(41):
                l = f.readline()
                if int(l[:2]) >= 20:
                    l_t = l.split(" ")
                    thrsum += float(l_t[1].replace("\n", ""))
    if nodeA+nodeB in through_act.keys():
        through_act[nodeA+nodeB] += thrsum/21/100000
    else:
        through_act[nodeA+nodeB] = thrsum/21/100000


for nodeA, nodeB in itertools.permutations(nodes, 2):
    losssum = 0
    for port in range(10):
        with open(os.path.join(os.path.dirname(__file__), f"Data/n{nodes.index(nodeA)}-n{nodes.index(nodeB)}-p{port+1}.loss"), "r") as f:
            for i in range(41):
                l = f.readline()
                if int(l[:2]) >= 20:
                    l_t = l.split(" ")
                    losssum += float(l_t[1].replace("\n", ""))

    if nodeA+nodeB in loss.keys():
        loss[nodeA+nodeB] += losssum/210
    else:
        loss[nodeA+nodeB] = losssum/210

if params.situ == "before":
    with open("od_data_before.py", "w") as odrate_b_f:
        print(f"actual = {repr(through_act)}", file=odrate_b_f)
        print(f"loss = {repr(loss)}", file=odrate_b_f)

elif params.situ == "after":
    with open("od_data_after.py", "w") as odrate_a_f:
        print(f"actual = {repr(through_act)}", file=odrate_a_f)
        print(f"loss = {repr(loss)}", file=odrate_a_f)
    from estimate_gamma import estimate_gamma
    gamma = estimate_gamma()
    for nodeA, nodeB in itertools.permutations(nodes, 2):
        through_lat[nodeA+nodeB] = (1 / math.exp(-gamma *
                                                loss[nodeA+nodeB])) * through_act[nodeA+nodeB]
    with open("od_data_after.py", "a") as odrate_a_f:
        print(f"latent = {repr(through_lat)}", file=odrate_a_f)

elif params.situ == "last":
    with open("od_data_last.py", "w") as odrate_l_f:
        print(f"actual = {repr(through_act)}", file=odrate_l_f)
        print(f"loss = {repr(loss)}", file=odrate_l_f)
        for nodeA, nodeB in itertools.permutations(nodes, 2):
            through_lat[nodeA+nodeB] = (1 / math.exp(-13.1 *
                                                     loss[nodeA+nodeB])) * through_act[nodeA+nodeB]
        print(f"latent = {repr(through_lat)}", file=odrate_l_f)
