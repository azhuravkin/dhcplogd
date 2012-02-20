CREATE TABLE `bindlog` (
  `id` bigint(10) unsigned NOT NULL auto_increment,
  `date` date default NULL,
  `time` time default NULL,
  `host_mac` char(17) default NULL,
  `leased_ip` char(15) default NULL,
  `hostname` char(40) default NULL,
  `relay` char(15) default NULL,
  `sw_mac` char(17) default NULL,
  `sw_ip` char(15) default NULL,
  `port` tinyint(3) unsigned default NULL,
  `vlan` smallint(5) unsigned default NULL,
  `ack` tinyint(1) default NULL,
  PRIMARY KEY  (`id`)
) ENGINE=MyISAM DEFAULT CHARSET=cp866;
