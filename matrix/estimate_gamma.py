import math
from od_data_before import actual as traffic_before, loss as loss_before
from od_data_after import actual as traffic_after, loss as loss_after

def estimate_gamma():
  gamma = []
  for od in traffic_before.keys():
    od_traffic_before = traffic_before[od]  # 経路制御前観測トラヒック
    od_traffic_after = traffic_after[od]    # 経路制御後観測トラヒック
    od_packet_loss_before = loss_before[od] # 経路制御前観測パケットロス率
    od_packet_loss_after = loss_before[od]  # 経路制御前観測パケットロス率

    if (od_packet_loss_before - od_packet_loss_after) != 0:
      gamma.append(math.log(od_traffic_before / od_traffic_after) / (od_packet_loss_before - od_packet_loss_after))
  
  if len(gamma) != 0:
    gamma_mean = sum(gamma) / len(gamma)
  else: 
    gamma_mean = 0
  with open("estimated_gamma.txt","w") as egf:
    print(repr(gamma), file=egf)
    print(gamma_mean, file=egf)

  return gamma_mean
