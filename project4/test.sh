echo "Make"
net_id=ks2025
make -f Makefile clean;  make -f Makefile
cd benchmark/;  make -f Makefile clean;  make -f Makefile; cd ..
echo "Simple test and remove everything"
./rufs -s /tmp/$net_id/mountdir/; cd benchmark/; ./simple_test; cd ..; fusermount -u /tmp/$net_id/mountdir; cat rufs_stats.log
./rufs -s /tmp/$net_id/mountdir/; ls -la /tmp/$net_id/mountdir/; rm -rf /tmp/$net_id/mountdir/*; ls -la /tmp/$net_id/mountdir/; fusermount -u /tmp/$net_id/mountdir; cat rufs_stats.log
echo "Intense test and remove everything"
./rufs -s /tmp/$net_id/mountdir/; cd benchmark/; ./test_case; cd ..; fusermount -u /tmp/$net_id/mountdir; cat rufs_stats.log
./rufs -s /tmp/$net_id/mountdir/; ls -la /tmp/$net_id/mountdir/; rm -rf /tmp/$net_id/mountdir/*; ls -la /tmp/$net_id/mountdir/; fusermount -u /tmp/$net_id/mountdir; cat rufs_stats.log
