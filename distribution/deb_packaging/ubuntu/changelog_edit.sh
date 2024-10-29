#!/bin/bash

DATENOW_TIMESTAMP=$(date "+%s");
DATENOW_YMD_HMSZ=$(date -d "@${DATENOW_TIMESTAMP}" +"%Y%m%d%H%M%S%z")
declare -A VERSION_CODENAME
VERSION_CODENAME[22.04]="jammy"
VERSION_CODENAME[24.04]="noble"
VERSION_CODENAME[24.10]="oracular"

COMMENT=${1:-Release of v${DATENOW_YMD_HMSZ}}

for version in ${!VERSION_CODENAME[@]}; do
  code=${VERSION_CODENAME[${version}]}
  cat << EOF > ${version}/new_cl_entry;
amarok (3:3.1.1+1SNAPSHOT${DATENOW_YMD_HMSZ}-0ubuntu1ppa1~ubuntu${version}.1) ${code}; urgency=medium

  * ${COMMENT}

 -- Pedro de Carvalho Gomes <pedrogomes81@gmail.com>  $(date -R -d "@${DATENOW_TIMESTAMP}")

EOF
 cat ${version}/new_cl_entry ${version}/debian/changelog > ${version}/new_cl
 mv ${version}/new_cl ${version}/debian/changelog
 rm ${version}/new_cl*;
done

