#!/usr/bin/env bash

source /etc/os-release

case $ID in
	centos)
		echo /usr/lib64
		;;
	fedora)
		echo /usr/lib64
		;;
	ubuntu)
		echo /usr/lib
esac
