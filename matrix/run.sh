#! /bin/bash
declare -A MAP
MAP[0]=A
MAP[1]=B
MAP[2]=C
MAP[3]=D
MAP[4]=E
MAP[5]=F
MAP[6]=G
MAP[7]=H
MAP[8]=I
MAP[9]=J
MAP[10]=K

for i in {0..0}; do
  for j in {10..10}; do
        if [ $i != $j ]; then
          # 初期シミュレーション作成
          echo $i-$j init
          cd ./matrix
          python3 ./create_sim.py --OrigNode ${MAP[$i]} --DestNode ${MAP[$j]} --Opt init >> created
          python3 ./combine.py
          rm ./created
          cd ../
          # 初期シミュレーション実行
          ./waf --run "created --OrigNode=${i} --DestNode=${j}" 1> /dev/null
          cd ./matrix
          # ODトラヒック集計 OD直接計測
          # python3 ./get_od_data.py --Situ before
          # ODトラヒック集計 OD推定
          python3 ./estimate_od_data.py --Situ before
          # クリーンアップ
          mkdir ../Data/$i-$j-init
          mkdir ../Data/$i-$j-init/p
          mv ./Data/* ../Data/$i-$j-init/p
          mv ./link.* ../Data/$i-$j-init
          mv ./orig_route.py ../Data/$i-$j-init
          cd ../scratch
          rm ./created.cc

          # 経路制御シミュレーション作成
          echo $i-$j TE actual
          cd ../matrix
          python3 ./create_sim.py --OrigNode ${MAP[$i]} --DestNode ${MAP[$j]} --Opt te >> created
          python3 ./combine.py
          rm ./created
          cd ../
          # 経路制御シミュレーション実行
          ./waf --run "created --OrigNode=${i} --DestNode=${j}" 1> /dev/null
          cd ./matrix
          # ODトラヒック集計 OD直接計測
          # python3 ./get_od_data.py --Situ last
          # ODトラヒック集計 OD推定
          python3 ./estimate_od_data.py --Situ after
          # クリーンアップ
          mkdir ../Data/$i-$j-after
          mkdir ../Data/$i-$j-after/p
          mv ./Data/* ../Data/$i-$j-after/p
          mv ./link.* ../Data/$i-$j-after
          mv ./util_opt_route.py ../Data/$i-$j-after
          cd ../scratch
          rm ./created.cc

          # 帯域設計経路制御シミュレーション作成
          echo $i-$j TECP estimated gamma
          cd ../matrix
          python3 ./create_sim.py --OrigNode ${MAP[$i]} --DestNode ${MAP[$j]} --Opt tecp >> created
          python3 ./combine.py
          rm ./created
          cd ../
          # 帯域設計経路制御シミュレーション実行
          ./waf --run "created --OrigNode=${i} --DestNode=${j}" 1> /dev/null
          cd ./matrix
          # ODトラヒック集計 OD直接計測
          python3 ./get_od_data.py --Situ last
          # クリーンアップ
          mkdir ../Data/$i-$j-last
          mkdir ../Data/$i-$j-last/p
          mv ./Data/* ../Data/$i-$j-last/p
          mv ./link.* ../Data/$i-$j-last
          mv ./util_capa_opt_route.py ../Data/$i-$j-last
          mv ./capas_incd.py ../Data/$i-$j-last
          mv ./od_data_before.py ../Data/$i-$j-init
          mv ./od_data_after.py ../Data/$i-$j-after
          mv ./od_data_last.py ../Data/$i-$j-last
          mv ./estimated_gamma.txt ../Data/$i-$j-last
          cd ../scratch
          rm ./created.cc
          cd ../
        fi
    done
done
