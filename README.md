# Sample Apps with Containers


## Build an app
```
git clone http://mod.lge.com/hub/timpani/sample-apps.git
cd sample-apps
mkdir build
cd build
cmake ..
cmake --build .
```


## Run an app
```
sudo chrt -f 20 ./sample_apps wakee1 10
sudo chrt -f 20 ./sample_apps wakee2 65
sudo chrt -f 20 ./sample_apps wakee3 8
```


## Build a Container (specially on Docker)
```
git clone http://mod.lge.com/hub/timpani/sample-apps.git
cd sample-apps
docker build -t IMAGE_NAME:TAG -f ./Dockerfile.release_name ./

ex)
docker build -t ubuntu_latest:sample_apps_v3 -f ./Dockerfile.ubuntu ./
```


## Run a Container (specially on Docker)
```
docker run -it --rm -d --cap-add=sys_nice --privileged --name CONTAINER_NAME IMAGE_NAME:TAG PROC_NAME PROC_PERIOD

ex)
container1: /* period: 10 ms, runtime: about 7ms */
docker run -it --rm -d --cpuset-cpus 2 --ulimit rtprio=99 --cap-add=sys_nice --privileged --name wakee1 ubuntu_latest:sample_apps_v3 wakee1 10
container2: /* period: 50 ms, runtime: about 40ms */
docker run -it --rm -d --cpuset-cpus 3 --ulimit rtprio=99 --cap-add=sys_nice --privileged --name wakee2 ubuntu_latest:sample_apps_v3 wakee2 65
container3: /* period: 20 ms, runtime: about 5ms */
docker run -it --rm -d --cpuset-cpus 2 --ulimit rtprio=99 --cap-add=sys_nice --privileged --name wakee3 ubuntu_latest:sample_apps_v3 wakee3 8
```

