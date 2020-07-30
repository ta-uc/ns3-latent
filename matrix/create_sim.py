from topology import nodes, links, links_all
from print_route import print_route
import argparse
import sys
import time

parser = argparse.ArgumentParser(description='Write out NS3 program')
parser.add_argument('--OrigNode', action="store", dest="orig_node", default="A")
parser.add_argument('--DestNode', action="store", dest="dest_node", default="K")
parser.add_argument('--OdRate', action="store", dest="od_rate", default="latent")
parser.add_argument('--Opt', action="store", dest="opt", default="init")
params = parser.parse_args()

if params.opt == "tecp":
  from opt_route_capa import opt_route_capa
  opt_route_capa(params.orig_node, params.dest_node, params.od_rate) #経路・帯域最適化
  from capas_incd import capas # 経路・帯域最適化後の帯域
  from util_capa_opt_route import route_lists #経路・帯域最適化後の経路

elif params.opt == "te":
  from opt_route import opt_route
  opt_route(params.orig_node, params.dest_node) # 十分帯域・初期トラヒックで経路最適化
  from topology import capas # 初期帯域
  from util_opt_route import route_lists # 経路最適化経路

elif params.opt == "init":
  from init_route import init_route
  init_route() # 初期経路を設定
  from topology import capas # 初期帯域
  from orig_route import route_lists # 初期経路

else:
  sys.exit()

print_route(route_lists, capas) # 経路情報書き出し
