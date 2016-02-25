FROM alanfranz/fwd-centos-7:latest
MAINTAINER Leif Madsen <leif@leifmadsen.com>
ENV REFRESHED_AT 2016-02-25
ADD contrib/scripts/install_prereq /tmp/install_prereq
RUN yum clean metadata && \
    yum -y update && \
    yum install epel-release -y && \
    yum clean all &&\
    /tmp/install_prereq install
