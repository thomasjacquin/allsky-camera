#!/bin/bash
source /home/pi/allsky/config.sh
source /home/pi/allsky/scripts/filename.sh

cd /home/pi/allsky/

RED='\033[0;31m'
GREEN='\033[0;32m'
NC='\033[0m' # No Color

if [ $# -lt 1 ]
  then
    echo -en "${RED}You need to pass a day argument\n"
        echo -en "    ex: timelapse.sh 20180119${NC}\n"
        exit 3
fi

# find thumbnails and rename them to match full images names
find "/home/pi/allsky/images/$1/thumbnails" -name "*.$EXTENSION" -size 0 -delete
ls -rt /home/pi/allsky/images/$1/thumbnails/*.$EXTENSION |
gawk 'BEGIN{ a=1 }{ printf "ln -s %s /home/pi/allsky/images/'$1'/thumbnails/%04d.'$EXTENSION'\n", $0, a++ }' |
bash

# find images, rename images sequentially and start avconv to build mp4; upload mp4 and move directory
find "/home/pi/allsky/images/$1" -name "*.$EXTENSION" -size 0 -delete
ls -rt /home/pi/allsky/images/$1/*.$EXTENSION |
gawk 'BEGIN{ a=1 }{ printf "ln -s %s /home/pi/allsky/images/'$1'/%04d.'$EXTENSION'\n", $0, a++ }' |
bash
ffmpeg -y -f image2 -r 25 -i /home/pi/allsky/images/$1/%04d.$EXTENSION -vcodec libx264 -b:v 2000k -pix_fmt yuv420p /home/pi/allsky/images/$1/allsky-$1.mp4

if [ "$UPLOAD_VIDEO" = true ] ; then
	lftp "$PROTOCOL"://"$USER":"$PASSWORD"@"$HOST":"$MP4DIR" -e "set net:max-retries 1; put /home/pi/allsky/images/$1/allsky-$1.mp4; bye"
fi

rm /home/pi/allsky/images/$1/thumbnails/????.$EXTENSION
rm /home/pi/allsky/images/$1/????.$EXTENSION
echo -en "* ${GREEN}Timelapse was created${NC}\n"
