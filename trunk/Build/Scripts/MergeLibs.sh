#! /bin/sh

LIBNAME=libBlueTune.a

rm $LIBNAME
for lib in *.a
do
echo $lib
sublib=${lib%.a}
mkdir $sublib
cd $sublib
ar x ../$lib
cd ..
ar rs $LIBNAME $sublib/*.o 
done