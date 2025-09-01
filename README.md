# Real-time Sample Applications

이 프로젝트는 실시간 시스템 분석을 위한 샘플 애플리케이션입니다. 주기적 실행, 데드라인 모니터링, 런타임 통계 수집 기능을 제공합니다.

## 주요 기능

- **주기적 실행**: 설정 가능한 주기로 작업 실행
- **데드라인 모니터링**: 데드라인 위반 감지 및 통계
- **런타임 측정**: 정확한 실행 시간 측정 및 통계 수집
- **실시간 우선순위**: SCHED_FIFO 스케줄링 정책 지원
- **다양한 워크로드**: Newton-Raphson 제곱근, 피보나치, 바쁜 대기 알고리즘
- **통계 보고서**: 최소/최대/평균 실행 시간, 데드라인 위반율 등

## 빌드 방법

```bash
git clone http://mod.lge.com/hub/timpani/sample-apps.git
cd sample-apps
mkdir build
cd build
cmake ..
cmake --build .
```

## 사용법

### 기본 문법
```bash
sudo ./sample_apps [OPTIONS] <task_name>
```

### 옵션

| 옵션 | 설명 | 기본값 |
|------|------|--------|
| `-p, --period PERIOD` | 주기 (밀리초) | 100 |
| `-d, --deadline DEADLINE` | 데드라인 (밀리초) | period와 동일 |
| `-r, --runtime RUNTIME` | 예상 실행시간 (마이크로초) | 50000 |
| `-P, --priority PRIORITY` | 실시간 우선순위 (1-99) | 50 |
| `-a, --algorithm ALGO` | 알고리즘 (1=NSQRT, 2=Fibonacci, 3=Busy loop) | 1 |
| `-l, --loops LOOPS` | 루프 횟수 (알고리즘 1,2) 또는 실행시간(알고리즘 3) | 10 |
| `-s, --stats` | 상세 통계 활성화 | 기본 활성화 |
| `-t, --timer` | 타이머 기반 주기 실행 | 신호 기반 |
| `-h, --help` | 도움말 표시 | - |

### 실행 예제

#### 1. 기본 실행
```bash
# 100ms 주기, 50ms 데드라인, 우선순위 50으로 실행
sudo ./sample_apps mytask
```

#### 2. 고성능 실시간 태스크
```bash
# 10ms 주기, 9ms 데드라인, 높은 우선순위(90)
sudo ./sample_apps -p 10 -d 9 -P 90 high_freq_task
```

#### 3. 피보나치 알고리즘 사용
```bash
# 50ms 주기, 피보나치 알고리즘, 5회 반복
sudo ./sample_apps -p 50 -a 2 -l 5 fibonacci_task
```

#### 4. 바쁜 대기 알고리즘
```bash
# 20ms 주기, 15ms 바쁜 대기 (15000us)
sudo ./sample_apps -p 20 -a 3 -l 15000 busy_task
```

#### 5. 타이머 기반 주기 실행
```bash
# 타이머를 사용한 자동 주기 실행 (외부 신호 불필요)
sudo ./sample_apps -t -p 100 timer_task
```

## 실행 모드

### 1. 신호 기반 모드 (기본)
- 외부에서 SIGRTMIN+2 신호를 보내야 작업이 실행됩니다
- 외부 스케줄러나 트리거 시스템과 연동할 때 유용합니다

```bash
# 터미널 1: 애플리케이션 실행
sudo ./sample_apps -p 50 -d 45 signal_task

# 터미널 2: 주기적으로 신호 전송
while true; do
    killall -s $(($(kill -l SIGRTMIN) + 2)) sample_apps
    sleep 0.05  # 50ms 주기
done
```

### 2. 타이머 기반 모드
- 내부 타이머를 사용하여 자동으로 주기적 실행
- 독립적인 실시간 태스크에 적합합니다

```bash
sudo ./sample_apps -t -p 50 -d 45 timer_task
```

## 통계 정보

애플리케이션은 다음과 같은 런타임 통계를 제공합니다:

- **최소/최대/평균 실행시간**: 마이크로초 단위
- **데드라인 위반 횟수 및 비율**: 전체 실행 대비 백분율
- **총 실행 횟수**: 완료된 주기 수
- **마지막 실행시간**: 가장 최근 실행의 소요 시간

### 출력 예제
```
=== Runtime Statistics for mytask ===
Iterations:      1000
Min runtime:     45230 us
Max runtime:     52100 us
Avg runtime:     48765 us
Last runtime:    49120 us
Deadline misses: 12 (1.20%)
Period:          50 ms
Deadline:        45 ms
Expected runtime: 50000 us
=====================================
```

## 컨테이너 실행

### Docker 빌드
```bash
# Ubuntu 기반 이미지 빌드
docker build -t ubuntu_latest:sample_apps_v3 -f ./Dockerfile.ubuntu ./

# CentOS 기반 이미지 빌드
docker build -t centos_latest:sample_apps_v3 -f ./Dockerfile.centos ./
```

### Docker 실행
```bash
# 고주파수 태스크 (10ms 주기)
docker run -it --rm -d \
    --cpuset-cpus 2 \
    --ulimit rtprio=99 \
    --cap-add=sys_nice \
    --privileged \
    --name high_freq_task \
    ubuntu_latest:sample_apps_v3 \
    sample_apps -t -p 10 -d 9 -P 90 container_task

# 중간 주파수 태스크 (50ms 주기)
docker run -it --rm -d \
    --cpuset-cpus 3 \
    --ulimit rtprio=99 \
    --cap-add=sys_nice \
    --privileged \
    --name medium_freq_task \
    ubuntu_latest:sample_apps_v3 \
    sample_apps -t -p 50 -d 45 -P 70 -a 2 -l 5 container_task
```

## 실시간 성능 튜닝

### 1. 시스템 설정
```bash
# CPU governor를 performance로 설정
echo performance | sudo tee /sys/devices/system/cpu/cpu*/cpufreq/scaling_governor

# IRQ 밸런싱 비활성화
sudo systemctl stop irqbalance

# 특정 CPU 코어 격리 (부팅 시 커널 파라미터)
# isolcpus=2,3 nohz_full=2,3 rcu_nocbs=2,3
```

### 2. 우선순위 설정 가이드
- **1-33**: 낮은 우선순위 (일반적인 실시간 태스크)
- **34-66**: 중간 우선순위 (중요한 실시간 태스크)
- **67-99**: 높은 우선순위 (매우 중요한 시스템 태스크)

### 3. 주기 및 데드라인 설정 권장사항
- **데드라인 < 주기**: 일반적인 실시간 시스템
- **데드라인 = 주기**: 주기적 태스크의 기본 설정
- **짧은 주기 (< 10ms)**: 고주파수 제어 시스템
- **긴 주기 (> 100ms)**: 모니터링 및 로깅 태스크

## 문제 해결

### 권한 문제
```bash
# 실시간 우선순위 설정을 위해 sudo 권한 필요
sudo ./sample_apps ...

# 또는 사용자에게 실시간 우선순위 권한 부여
echo "username - rtprio 99" | sudo tee -a /etc/security/limits.conf
```

### 성능 문제
- CPU 사용률이 100%에 가까우면 주기를 늘리거나 루프 횟수를 줄이세요
- 데드라인 위반이 자주 발생하면 데드라인을 늘리거나 작업량을 줄이세요
- 시스템 노이즈를 줄이기 위해 불필요한 서비스를 중지하세요

### 신호 문제
- 신호 기반 모드에서 신호가 전달되지 않으면 프로세스 이름과 PID를 확인하세요
- 여러 인스턴스가 실행 중이면 특정 PID로 신호를 보내세요

## 라이센스

이 프로젝트는 LG Electronics의 Timpani 프로젝트의 일부입니다.