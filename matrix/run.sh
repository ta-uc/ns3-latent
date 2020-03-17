#! /bin/bash

for i in {3..9}; do
  for j in {0..10}; do
        if [ $i -ne $j ]; then 
            ./waf --run "internet2 --OrigNode=${i} --DestNode=${j}"
            python3 ./matrix/matrix.py
            mkdir "./Data/${i}-${j}-b"
            mv ./matrix/link.* "./Data/${i}-${j}-b/"
            ./waf --run "internet2 --OrigNode=${i} --DestNode=${j} --FileName=./matrix/capas_od"
            mkdir "./Data/${i}-${j}-od"
            mv ./matrix/link.* "./Data/${i}-${j}-od/"
            ./waf --run "internet2 --OrigNode=${i} --DestNode=${j} --FileName=./matrix/capas_sep"
            mkdir "./Data/${i}-${j}-sep"
            mv ./matrix/link.* "./Data/${i}-${j}-sep/"
            rm ./matrix/capas_od
            rm ./matrix/capas_sep
        fi
    done
done