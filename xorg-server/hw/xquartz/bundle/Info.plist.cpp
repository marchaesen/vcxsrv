<?xml version="1.0" encoding="UTF-8"?>
<!DOCTYPE plist PUBLIC "-//Apple Computer//DTD PLIST 1.0//EN" "http://www.apple.com/DTDs/PropertyList-1.0.dtd">
<plist version="1.0">
<dict>
	<key>CFBundleDevelopmentRegion</key>
		<string>English</string>
	<key>CFBundleExecutable</key>
		<string>X11</string>
	<key>CFBundleGetInfoString</key>
		<string>BUNDLE_ID_PREFIX.X11</string>
	<key>CFBundleIconFile</key>
		<string>X11.icns</string>
	<key>CFBundleIdentifier</key>
		<string>BUNDLE_ID_PREFIX.X11</string>
	<key>CFBundleInfoDictionaryVersion</key>
		<string>6.0</string>
	<key>CFBundleName</key>
		<string>APPLE_APPLICATION_NAME</string>
	<key>CFBundlePackageType</key>
		<string>APPL</string>
	<key>CFBundleShortVersionString</key>
		<string>2.7.7</string>
	<key>CFBundleVersion</key>
		<string>2.7.7</string>
	<key>CFBundleSignature</key>
		<string>x11a</string>
	<key>CSResourcesFileMapped</key>
		<true/>
	<key>NSSupportsAutomaticGraphicsSwitching</key>
		<true/>
#ifdef XQUARTZ_SPARKLE
	<key>SUEnableAutomaticChecks</key>
		<true/>
	<key>SUPublicDSAKeyFile</key>
		<string>sparkle.pem</string>
        <key>SUFeedURL</key>
                <string>http://xquartz.macosforge.org/downloads/sparkle/release.xml</string>
	<key>NSAppTransportSecurity</key>
	<dict>
		<key>NSExceptionDomains</key>
		<dict>
			<key>macosforge.org</key>
			<dict>
				<key>NSIncludesSubdomains</key>
				<true/>
				<key>NSExceptionAllowsInsecureHTTPLoads</key>
				<true/>
			</dict>
		</dict>
	</dict>
#endif
	<key>LSApplicationCategoryType</key>
		<string>public.app-category.utilities</string>
	<key>NSHumanReadableCopyright</key>
		<string>© 2003-2013 Apple Inc.
© 2003 XFree86 Project, Inc.
© 2003-2013 X.org Foundation, Inc.
</string>
	<key>NSMainNibFile</key>
		<string>main</string>
	<key>NSPrincipalClass</key>
		<string>X11Application</string>
</dict>
</plist>
