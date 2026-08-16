# Build Guidance

## Primary Build Path

The primary full-stack build entrypoint for this repository is Docker Compose:

```sh
cd docker
docker compose build
```

This path builds:

- the React production bundle through `docker/nginx_fastdfs/dockerfile`
- the FastCGI application image through `docker/fastcgi_app/dockerfile`
- the MySQL image through `docker/mysql/dockerfile`

## Targeted Build Paths

Use targeted builds only when the task is clearly isolated:

- frontend bundle only:
  `cd picture_bed && npm run build`
- frontend container refresh:
  `./update_frontend.sh`
- backend local Linux build with host dependencies already installed:
  `make clean && make`

## Environment Assumptions

- `make clean && make` depends on Linux-native FastDFS, FastCGI, MySQL, Redis,
  FAISS, and related headers and libraries
- on this Windows workstation, local backend compilation may be unavailable or
  unrepresentative, so Docker is the safer default verification path
- helper scripts must not install dependencies automatically

## Artifact Notes

- frontend local build output: `picture_bed/build/`
- backend build output: `bin_cgi/`
- container runtime topology is defined only by `docker/docker-compose.yaml`
