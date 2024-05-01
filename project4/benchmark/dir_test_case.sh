max=256
CURR_HOME=/tmp/ks2025

for i in `seq 1 $max`
do
    echo "make $i"
    mkdir $CURR_HOME/mountdir/test_$i
done

for i in `seq 1 $max`
do
    echo "remove $i"
    mkdir $CURR_HOME/mountdir/test_$i/test_$i
done

for i in `seq 1 $max`
do
    echo "remove $i"
    rmdir $CURR_HOME/mountdir/test_$i
done

for i in `seq 1 $max`
do
    echo "touch $i"
    echo "folder going to be removed $i..." > $CURR_HOME/mountdir/test_$i/test_$i.txt
done

for i in `seq 1 $max`
do
    cat $CURR_HOME/mountdir/test_$i/test_$i.txt
    rm -rf $CURR_HOME/mountdir/test_$i
done
