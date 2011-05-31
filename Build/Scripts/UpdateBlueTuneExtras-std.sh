for name in x86-unknown-linux x86-microsoft-win32 universal-apple-macosx arm-chumby-linux
do
echo $name
python UpdateBlueTuneExtras.py $name ~/Workspace/BlueTuneExtras
done
