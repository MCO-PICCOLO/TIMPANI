#!/bin/sh

if [ $# -ne 2 ]; then
	echo "Usage: $0 input_file output_file"
	exit 1
fi

GP_SCRIPT_DIR=$(dirname $0)
GP_INPUT_FILE=$1
GP_OUTPUT_FILE=$2

export GP_SCRIPT_DIR
export GP_INPUT_FILE

exec gnuplot $GP_SCRIPT_DIR/ttsched.gpi > $GP_OUTPUT_FILE
