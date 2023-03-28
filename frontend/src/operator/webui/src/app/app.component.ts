import {Component, HostListener, Injectable} from '@angular/core';

import {DisplaysService} from './displays.service';

@Injectable()
@Component({
  selector: 'app-root',
  templateUrl: './app.component.html',
  styleUrls: ['./app.component.scss'],
})
export class AppComponent {
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
