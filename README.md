# Sample Apps with Containers


## Build an app
```
git clone http://mod.lge.com/hub/timpani/sample-apps.git
cd sample-apps
mkdir build
cd build
cmake ..
make
```


## Build a Container (specially on Docker)
```
git clone http://mod.lge.com/hub/timpani/sample-apps.git
cd sample-apps
docker build -t IMAGE_NAME:TAG -f ./Dockerfile ./
```


## Run a Container (specially on Docker)
```
docker run -it --rm -d --cap-add=sys_nice --privileged --name CONTAINER_NAME IMAGE_NAME:TAG PROC_NAME PROC_PERIOD
(ex, docker run -it --rm -d --cpu-rt-runtime=15000 --ulimit rtprio=99 --cap-add=sys_nice --privileged --name wakee1 centos_stream9:cmake_build_sample-apps_and_entrypoint_v04 wakee1 20000)
```

