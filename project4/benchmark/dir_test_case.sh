max=20
CURR_HOME=/tmp/ks2025

for i in `seq 1 $max`
do
    echo "make $i"
    mkdir $CURR_HOME/mountdir/test_$i
done

for i in `seq 1 $max`
do
    echo "remove $i"
    rmdir $CURR_HOME/mountdir/test_$i
done
