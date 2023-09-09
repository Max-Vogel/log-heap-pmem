input_path="../../workloads/"

#array-dram
for file in $input_path*.spec; do
  n_threads=1
  while [ $n_threads -le 1 ]
  do
    counter=1
    echo "[Benchmark] malloc-dram, #of_threads: " $n_threads ", workload: ${file##*/}"
    while [ $counter -le 10 ]
    do
      ./../../ycsbc -db storeds -threads $n_threads -dbpath ../../test_pmem -type malloc-dram -P $input_path${file##*/}
      ((counter++))
    done
    echo "*****************<>*****************"
    ((n_threads*=2))
  done
  echo "~~~~~~~~~~~~~~~~~~~~~~~~~<>~~~~~~~~~~~~~~~~~~~~~~~~~"
done
