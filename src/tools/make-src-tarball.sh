# !/bin/bash

#
# Make src tarball and place next to the spec file for rpmbuild
#

#
# version
#
version=0.1

#
# staging directory
#
stageDir=vios-proxy-${version}
if [ -d ${stageDir} ];
then
    cd ${stageDir}
    rm -rf .
    cd ..
else
    mkdir ${stageDir}
fi

#
# populate the stage
#
curDir=`pwd`
pushd ../src
cp -r * ${curDir}/${stageDir}
popd

#
# make the zip
#
tar czf vios-proxy-${version}.tar.gz ${stageDir}

#
# copy in the spec file
#
cp ../src/rpm-spec/vios-proxy.spec .

#
# clean up the stage
#
rm -rf ${stageDir}

#
# show the user what he's got
#
echo Your files are:
ls vios*
