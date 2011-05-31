#mkdir tmp
#cd tmp
libs= 
for name in h264_dec vm vm_plus color_space_converter umc
do
libs="$libs $1/lib$name.a"
done

for name in ippdc_l ippcc_l ippac_l ippvc_l ippj_l ippi_l ipps_l ippcv_l ippsc_l ippcore_l
do
libs="$libs /opt/intel/ipp/lib/lib$name.a"
done

#ar rs libippmedia.a *.o
#mv libippmedia.a ..
#cd ..
#rm -rf tmp
echo $libs
libtool -v -o libippmedia.a $libs