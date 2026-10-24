// NOVA OS Plasma 6 desktop layout
(function () {
  const previous = panels();
  const menu = new Panel();
  menu.location = 'top';
  menu.height = 34;
  menu.lengthMode = 'fill';
  menu.hiding = 'none';
  menu.addWidget('org.kde.plasma.kickoff');
  menu.addWidget('org.kde.plasma.appmenu');
  menu.addWidget('org.kde.plasma.panelspacer');
  menu.addWidget('org.kde.plasma.systemtray');
  menu.addWidget('org.kde.plasma.digitalclock');
  const dock = new Panel();
  dock.location = 'bottom';
  dock.height = 58;
  dock.lengthMode = 'fit';
  dock.alignment = 'center';
  dock.hiding = 'dodgewindows';
  const tasks = dock.addWidget('org.kde.plasma.icontasks');
  if (tasks) {
    tasks.currentConfigGroup = ['General'];
    tasks.writeConfig('launchers',
      'applications:org.kde.dolphin.desktop,' +
      'applications:org.kde.konsole.desktop,' +
      'applications:org.kde.kate.desktop,' +
      'applications:firefox-esr.desktop,' +
      'applications:org.kde.discover.desktop');
  }
  dock.addWidget('org.kde.plasma.showdesktop');
  for (const desktop of desktops()) {
    desktop.wallpaperPlugin = 'org.kde.image';
    desktop.currentConfigGroup = ['Wallpaper','org.kde.image','General'];
    desktop.writeConfig('Image','file:///usr/share/wallpapers/NOVA/nova-light.png');
    desktop.writeConfig('FillMode',2);
  }
  for (const p of previous) p.remove();
})();
