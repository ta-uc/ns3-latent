import math
from od_data_before import actual as traffic_before, loss
from od_data_after import actual as traffic_after

def estimate_gamma():
  gamma = []
  for od in traffic_before.keys():
    od_traffic_bf_te = traffic_before[od] # 経路制御前観測トラヒック
    od_traffic_at_te = traffic_after[od] # 経路制御後観測トラヒック
    od_packet_loss = loss[od] # 経路制御前観測パケットロス率
    if od_packet_loss != 0 and od_traffic_bf_te != od_traffic_at_te:
      gamma.append(math.log(od_traffic_bf_te / od_traffic_at_te) / od_packet_loss)
  gamma_mean = sum(gamma) / len(gamma)
  with open("estimated_gamma.txt","w") as egf:
    print(repr(gamma), file=egf)
    print(gamma_mean, file=egf)
  return gamma_mean
