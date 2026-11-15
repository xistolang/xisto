/**
 * On a phone, a docs page opens the menu on the docs sidebar (the "secondary" menu), and Docusaurus keeps the
 * version dropdown in the main menu only, behind "Back to main menu". The versions go on top of the docs sidebar
 * too, so another version is one tap away on every docs page.
 */
import React from 'react';
import SecondaryMenu from '@theme-original/Navbar/MobileSidebar/SecondaryMenu';
import NavbarItem from '@theme/NavbarItem';

export default function SecondaryMenuWrapper(props) {
  return (
    <>
      <ul className="menu__list xisto-mobile-versions">
        <NavbarItem type="docsVersionDropdown" mobile />
      </ul>
      <SecondaryMenu {...props} />
    </>
  );
}
