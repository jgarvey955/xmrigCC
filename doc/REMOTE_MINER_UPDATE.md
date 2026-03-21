# Remote Miner Update

Starting with version 3.2.0 onwards the miner is remote upgradable without user interaction on the miner.
This helps you a lot when maintaining large farms. Single click on the Dashboard and the miner will update to the latest version. \o/ Yeay!

## Requirements

* Server AND Miner (xmrigDaemon + xmrigMiner) running at least v3.2.0
* Latest update bundle (*-CLIENT-UPDATE-PACKAGE_only.*) extracted on the Server into the defined `client-updates/` folder

## Howto

1. Download the latest "*-CLIENT-UPDATE-PACKAGE_only.*" ([here](https://github.com/Bendr0id/xmrigCC/releases))
2. Extract everything inside that bundle into your xmrigCC-Server "/client-updates/" folder
3. If it asks to override (older version), press [YES]
4. Don't change folder-structure or namings here, it will break it.
5. Open the XMRigCC dashboard, select the miner (be kind to your server, not 1000 at once) and press the "[Update miner]" button
6. Miner will download, stop, patch and re-launch the new version

## FAQ

    Q: How is the update downloaded to the miner, do my miner need an internet connection?
    A: The update is downloaded through the same channel which is used for communicating with the CC-Server, no internet connection needed.

    Q: I have my miner binary renamed (daemon and/or miner) how does that work?
    A: The update process patches the downloaded binary and respects the renaming. 
       Please keep the original filename in the update bundle. Don't rename it here, it won't work. 

    Q: Where to find the update bundle?
    A: Starting with first release after 3.2.0 the release page will have a dedicated "-client-update-bundle-" package.
       Just download that one, extract into your server update folder (default: "/client-updates/"), don't rename anything here, it wont work.

    Q: I have an os/arch/custom binary which is not part of the update bundle can i still use the remote upgrade feature?
    A: Yes that is possible. If you follow the same naming/folder structure pattern you can use your own binares.
       For example MacOS-arm64: place your "xmrigMiner" binary (don't rename it) MacOS-arm build into a new folder "macos-arm64", and it will work.
