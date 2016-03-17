# Building Asterisk into a Docker Container Image
The following set of steps should leave you with a Docker container that
is relatively small, built from your local checked out source, and even
provides you with a nice little RPM too!

## Build the package container image
Build the package container image. This uses FPM[1] so no `spec` files and
such are necessary.
```
docker build --pull -f contrib/docker/Dockerfile.packager -t asterisk-build .
```

## Build your Asterisk RPM from source
Build the Asterisk RPM from the resulting container image.
```
docker run -ti \
    -v $(pwd):/application:ro \
    -v $(pwd)/out:/build \
    -w /application asterisk-build \
    /application/contrib/docker/make-package.sh 13.6.0
```
> **NOTE**: If you need to build this on a system that has SElinux enabled
> you'll need to use the following command instead:
> ```
> docker run -ti \
>     -v $(pwd):/application:Z \
>     -v $(pwd)/out:/build:Z \
>     -w /application asterisk-build \
>     /application/contrib/docker/make-package.sh 13.6.0
> ```

## Create your Asterisk container image
Now create your own Asterisk container image from the resulting RPM.
```
docker build --rm -t madsen/asterisk:13.6.0-1 -f contrib/docker/Dockerfile.asterisk .
```

# References
[1] https://github.com/jordansissel/fpm
