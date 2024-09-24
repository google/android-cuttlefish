import {Component, HostListener, Injectable} from '@angular/core';

import {DisplaysService} from './displays.service';
import {BUILD_VERSION} from '../environments/version'

@Injectable()
@Component({
  standalone: false,
  selector: 'app-root',
  templateUrl: './app.component.html',
  styleUrls: ['./app.component.scss'],
})
export class AppComponent {
  readonly version = BUILD_VERSION;
  constructor(private displaysService: DisplaysService) {}

  @HostListener('window:message', ['$event'])
  onWindowMessage(e: MessageEvent) {
    if (e.origin === window.location.origin) {
      const message = e.data;

      if (!('type' in message && 'payload' in message)) return;

      switch (message.type) {
        case 'displays_info': {
          this.displaysService.onDeviceDisplayInfo(message.payload);
          break;
        }
      }
    }
  }
}
