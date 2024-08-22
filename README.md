# Time Trigger



## Getting started

![TT_1](tt_1.PNG)
![TT_2](tt_2.PNG)
![TT_3](tt_3.PNG)


## Build

```
git clone http://mod.lge.com/hub/timpani/time-trigger.git
cd time-trigger
mkdir build
cd build
cmake ..
make
```

## How to use

execute sample wakee1 process in terminal 1
```
cd build
sudo ./exprocs wakee1 10000
```

execute sample wakee2 process in terminal 2
```
cd build
sudo ./exprocs wakee2 50000
```

execute sample wakee3 process in terminal 3
```
cd build
sudo ./exprocs wakee3 20000
```

execute time trigger in other terminal
```
cd build
sudo ./timetrigger pid_wakee1 pid_wakee2 pid_wakee3
```

***
