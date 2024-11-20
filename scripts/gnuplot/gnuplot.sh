#!/bin/sh

if [ $# -ne 1 ]; then
	echo "Usage: $0 input_file"
	exit 1
fi

GP_SCRIPT_DIR=$(dirname $0)
GP_INPUT_FILE=$1
GP_OUTPUT_FILE_BASE=${GP_INPUT_FILE%%.*}
GP_OUTPUT_FILE_TASK="$GP_OUTPUT_FILE_BASE-task.png"
GP_OUTPUT_FILE_CPU="$GP_OUTPUT_FILE_BASE-cpu.png"

export GP_SCRIPT_DIR
export GP_INPUT_FILE

gnuplot $GP_SCRIPT_DIR/ttsched.gpi > $GP_OUTPUT_FILE_TASK
gnuplot $GP_SCRIPT_DIR/ttsched-cpu.gpi > $GP_OUTPUT_FILE_CPU

exit 0
