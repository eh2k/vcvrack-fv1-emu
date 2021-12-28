set -e
cd $(dirname $0)

docker run --rm --user 1000:1000 -v $(realpath ..):/home/ --workdir=/home/ vcv-build-env make

rsync -avh --existing .. ~/.Rack2/plugins/EH_modules/