# Implements the equivalent of ci-templates container-ifnot-exists, using
# Docker directly as we don't have buildah/podman/skopeo available under
# Windows, nor can we execute Docker-in-Docker
$registry_uri = $args[0]
$registry_username = $args[1]
$registry_password = $args[2]
$registry_user_image = $args[3]
$registry_central_image = $args[4]

Set-Location -Path ".\.gitlab-ci\windows"

docker login -u "$registry_username" -p "$registry_password" "$registry_uri"
if (!$?) {
  Write-Host "docker login failed to $registry_uri"
  Exit 1
}

# if the image already exists, don't rebuild it
docker pull "$registry_user_image"
if ($?) {
  Write-Host "User image $registry_user_image already exists; not rebuilding"
  docker logout "$registry_uri"
  Exit 0
}

# if the image already exists upstream, copy it
docker pull "$registry_central_image"
if ($?) {
  Write-Host "Copying central image $registry_central_image to user image $registry_user_image"
  docker tag "$registry_central_image" "$registry_user_image"
  docker push "$registry_user_image"
  $pushstatus = $?
  docker logout "$registry_uri"
  if (!$pushstatus) {
    Write-Host "Pushing image to $registry_user_image failed"
    Exit 1
  }
  Exit 0
}

Write-Host "No image found at $registry_user_image or $registry_central_image; rebuilding"
docker build --no-cache -t "$registry_user_image" .
if (!$?) {
  Write-Host "Container build failed"
  docker logout "$registry_uri"
  Exit 1
}
Get-Date

docker push "$registry_user_image"
$pushstatus = $?
docker logout "$registry_uri"
if (!$pushstatus) {
  Write-Host "Pushing image to $registry_user_image failed"
  Exit 1
}
