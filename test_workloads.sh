#!/bin/bash

# Workload Performance Test Script
# 다양한 워크로드의 성능을 측정하고 비교하는 스크립트

echo "=========================================="
echo "Real-time Sample Apps - Workload Test"
echo "=========================================="

BINARY="./sample_apps"
TEST_DURATION=3  # seconds

if [ ! -f "$BINARY" ]; then
    echo "Error: $BINARY not found. Please build the project first."
    echo "Run: cmake .. && make"
    exit 1
fi

# Test function
run_test() {
    local name=$1
    local algo=$2
    local loops=$3
    local period=$4
    local deadline=$5

    echo "Testing $name (Algorithm $algo, Loops $loops)..."
    timeout ${TEST_DURATION}s $BINARY -t -p $period -d $deadline -a $algo -l $loops test_$name 2>/dev/null | \
        tail -10 | grep "Runtime Statistics" -A 10 | grep -E "(Min|Max|Avg|Deadline misses)" | \
        sed 's/^/  /'
    echo ""
}

echo "1. Lightweight Workloads Test"
echo "------------------------------"
run_test "nsqrt_light" 1 5 100 90
run_test "crypto_light" 6 5 100 90

echo "2. Medium Workloads Test"
echo "------------------------"
run_test "matrix_medium" 4 5 100 90
run_test "mixed_medium" 7 3 100 90

echo "3. Heavy Workloads Test"
echo "-----------------------"
run_test "memory_heavy" 5 8 200 180
run_test "prime_heavy" 8 10 200 180

echo "4. Runtime Scaling Test"
echo "-----------------------"
echo "Matrix workload scaling (different sizes):"
for size in 3 5 8 10; do
    echo "  Matrix size factor $size:"
    timeout 2s $BINARY -t -p 200 -d 180 -a 4 -l $size matrix_scale_$size 2>/dev/null | \
        grep "Runtime:" | head -5 | awk '{print "    " $3 " " $4}' | sort -n | head -3
done

echo ""
echo "5. Memory workload scaling (different sizes):"
for size in 4 8 16 32; do
    echo "  Memory size ${size}MB:"
    timeout 2s $BINARY -t -p 500 -d 450 -a 5 -l $size memory_scale_$size 2>/dev/null | \
        grep "Runtime:" | head -3 | awk '{print "    " $3 " " $4}' | sort -n | head -2
done

echo ""
echo "=========================================="
echo "Test completed. Use these results to:"
echo "1. Choose appropriate workload for your needs"
echo "2. Set realistic period and deadline values"
echo "3. Estimate system performance requirements"
echo "=========================================="
