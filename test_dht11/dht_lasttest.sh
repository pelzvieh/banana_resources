#!/bin/bash
mydir=$(dirname $0)
echo -e "#\treal[s]\tuser[s]\tsys[s]\tsuccess\tEIO\ttimeout\terr per succ"
for it in $(seq 10);do
   echo -en $it \\t
   /usr/bin/time -f "%e %U %S" -o /dev/stdout $mydir/dht_lasttest.py 2>/dev/null | awk '
{
  success=$2;
  inval=$3;
  timeout=$4;
  getline;
  wall=$1;
  usertime=$2;
  systemtime=$3;
  print wall"\t"usertime"\t"systemtime"\t"success"\t"inval"\t"timeout"\t"(inval+timeout)/success;

  getline;
}'
done
echo
