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
docker build -t IMAGE_NAME:TAG -f ./Dockerfile.release_name ./
  (ex: docker build -t ubuntu_latest:sample_apps_v1 -f ./Dockerfile.ubuntu ./)
```


## Run a Container (specially on Docker)
```
docker run -it --rm -d --cap-add=sys_nice --privileged --name CONTAINER_NAME IMAGE_NAME:TAG PROC_NAME PROC_PERIOD

ex)
task1: docker run -it --rm -d --cpu-rt-runtime=100000 --cpuset-cpus 2 --ulimit rtprio=99 --cap-add=sys_nice --privileged --name wakee1 ubuntu_latest:sample_apps_v2 wakee1 10000 10) /* period: 10 ms, runtime: 6ms (1 in '3rd arg' means about 600us cpu workload, so 10 means about 6ms cpu workload) */
task2: docker run -it --rm -d --cpu-rt-runtime=100000 --cpuset-cpus 3 --ulimit rtprio=99 --cap-add=sys_nice --privileged --name wakee2 ubuntu_latest:sample_apps_v2 wakee2 50000 50) /* period: 50 ms, runtime: 30ms (1 in '3rd arg' means about 600us cpu workload, so 50 means about 30ms cpu workload) */
task3: docker run -it --rm -d --cpu-rt-runtime=100000 --cpuset-cpus 2 --ulimit rtprio=99 --cap-add=sys_nice --privileged --name wakee3 ubuntu_latest:sample_apps_v2 wakee3 20000 20) /* period: 20 ms, runtime: 12ms (1 in '3rd arg' means about 600us cpu workload, so 20 means about 12ms cpu workload) */
```

