XSOCK=/tmp/.X11-unix
XAUTH=/tmp/.docker.xauth
touch $XAUTH
xauth nlist $DISPLAY| sed -e 's/^..../ffff/' | xauth -f $XAUTH nmerge -

docker run --rm -ti -e DISPLAY=$DISPLAY -v $XSOCK:$XSOCK -v $XAUTH:$XAUTH -v $(pwd):/home -e XAUTHORITY=$XAUTH rdp-replay $@
