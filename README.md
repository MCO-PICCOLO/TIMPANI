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
ex1) docker run -it --rm -d --cpu-rt-runtime=15000 --cpuset-cpus 1 --ulimit rtprio=99 --cap-add=sys_nice --privileged --name wakee1 centos_stream9:sample-apps_v01 wakee1 20000
ex2) docker run -it --rm -d --cpu-rt-runtime=15000 --cpuset-cpus 2 --ulimit rtprio=99 --cap-add=sys_nice --privileged --name wakee2 centos_stream9:sample-apps_v01 wakee2 50000
ex3) docker run -it --rm -d --cpu-rt-runtime=15000 --cpuset-cpus 3 --ulimit rtprio=99 --cap-add=sys_nice --privileged --name wakee3 centos_stream9:sample-apps_v01 wakee3 10000
```

