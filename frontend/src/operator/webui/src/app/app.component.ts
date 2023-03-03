import {Component, Injectable, HostListener} from '@angular/core';
import {DisplaysService} from './displays.service';
import {DeviceFrameMessage} from '../../../intercept/js/server_connector';

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

      if (!(message instanceof DeviceFrameMessage)) return;

      switch (message.type) {
        case DeviceFrameMessage.TYPE_DISPLAYS_INFO: {
          this.displaysService.onDeviceDisplayInfo(message.payload);
          break;
        }
      }
    }
  }
}
