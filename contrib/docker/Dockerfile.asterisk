# Version 0.0.3
FROM centos:7
MAINTAINER Leif Madsen <leif@leifmadsen.com>
ENV REFRESHED_AT 2016-02-25
ENV STARTDIR /tmp
ENV RPMPATH ./out

# copy is required because you can't mount volumes during build
COPY $RPMPATH/*.rpm $STARTDIR

# install dependencies and Asterisk RPM
RUN yum install epel-release -y && \
    yum install -y *.rpm && \
    yum clean all && \
    yum autoremove -y && \
    /sbin/ldconfig

ENTRYPOINT ["/usr/sbin/asterisk"]
CMD ["-c", "-vvvv", "-g"]
