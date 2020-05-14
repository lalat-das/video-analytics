# Dockerfile for VideoAnalytics
ARG EIS_VERSION
ARG DOCKER_REGISTRY
FROM ${DOCKER_REGISTRY}ia_openvino_base:$EIS_VERSION as openvino
LABEL description="VideoAnalytics image"

WORKDIR ${PY_WORK_DIR}
ARG EIS_UID
ARG EIS_USER_NAME
RUN useradd -r -u ${EIS_UID} -G video ${EIS_USER_NAME}

# Installing dependent python modules - needed by opencv
COPY va_requirements.txt .
RUN pip3.6 install -r va_requirements.txt && \
    rm -rf va_requirements.txt

FROM ${DOCKER_REGISTRY}ia_common:$EIS_VERSION as common
FROM ${DOCKER_REGISTRY}ia_video_common:$EIS_VERSION as video_common

FROM openvino

WORKDIR ${GO_WORK_DIR}

COPY --from=common /usr/local/include /usr/local/include
COPY --from=common /usr/local/lib /usr/local/lib
COPY --from=common ${GO_WORK_DIR}/common/cmake ./common/cmake
COPY --from=common ${GO_WORK_DIR}/common/libs ./common/libs
COPY --from=common ${GO_WORK_DIR}/common/util ${GO_WORK_DIR}/common/util
COPY --from=common /usr/local/lib/python3.6/dist-packages/ /usr/local/lib/python3.6/dist-packages

ARG CMAKE_BUILD_TYPE

COPY --from=video_common ${GO_WORK_DIR}/common/UDFLoader ./common/libs/UDFLoader
COPY --from=video_common ${GO_WORK_DIR}/common/udfs ./common/udfs

# Build UDF loader lib
RUN /bin/bash -c "source /opt/intel/openvino/bin/setupvars.sh && \
     cd ./common/libs/UDFLoader && \
     rm -rf build && \
     mkdir build && \
     cd build && \
     cmake -DCMAKE_BUILD_TYPE=${CMAKE_BUILD_TYPE} .. && \
     make install"

COPY --from=video_common ${GO_WORK_DIR}/common/udfs/native ./common/udfs/native

# Build native UDF samples
RUN /bin/bash -c "source /opt/intel/openvino/bin/setupvars.sh && \
    cd ./common/udfs/native && \
    rm -rf build && \
    mkdir build && \
    cd build && \
    cmake -DCMAKE_BUILD_TYPE=${CMAKE_BUILD_TYPE} .. && \
    make && \
    make install"

ENV LD_LIBRARY_PATH ${LD_LIBRARY_PATH}:/usr/local/lib/udfs/

# Adding project depedency modules
COPY . ./VideoAnalytics/

RUN chmod +x ./VideoAnalytics/va_classifier_start.sh

RUN /bin/bash -c "source /opt/intel/openvino/bin/setupvars.sh && \
    cd ./VideoAnalytics && \
    rm -rf build && \
    mkdir build && \
    cd build && \
    cmake -DCMAKE_BUILD_TYPE=${CMAKE_BUILD_TYPE} .. && \
    make"

# Removing build dependencies
RUN apt-get remove -y wget && \
    apt-get remove -y git && \
    apt-get remove -y curl && \
    apt-get autoremove -y

COPY --from=video_common ${GO_WORK_DIR}/common/udfs/python ./common/udfs/python

ENV PYTHONPATH ${PYTHONPATH}:${GO_WORK_DIR}/common/udfs/python:${GO_WORK_DIR}/common/

ENTRYPOINT ["VideoAnalytics/va_classifier_start.sh"]

