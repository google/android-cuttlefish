import {NgModule} from '@angular/core';
import {BrowserModule} from '@angular/platform-browser';
import {FormsModule} from '@angular/forms';

import {AppComponent} from './app.component';
import {DevicePaneComponent} from './device-pane/device-pane.component';
import {BrowserAnimationsModule} from '@angular/platform-browser/animations';
import {HttpClientModule} from '@angular/common/http';
import {MatButtonModule} from '@angular/material/button';
import {MatCardModule} from '@angular/material/card';
import {MatCheckboxModule} from '@angular/material/checkbox';
import {MatIconModule} from '@angular/material/icon';
import {MatMenuModule} from '@angular/material/menu';
import {MatSidenavModule} from '@angular/material/sidenav';
import {MatSlideToggleModule} from '@angular/material/slide-toggle';
import {MatSnackBarModule} from '@angular/material/snack-bar';
import {MatToolbarModule} from '@angular/material/toolbar';
import {ViewPaneComponent} from './view-pane/view-pane.component';
import {SafeDeviceUrlPipe} from './safe-device-url.pipe';
import {KtdGridModule} from '@katoid/angular-grid-layout';
import {RouterModule} from '@angular/router';

@NgModule({
  declarations: [
    AppComponent,
    DevicePaneComponent,
    ViewPaneComponent,
    SafeDeviceUrlPipe,
  ],
  imports: [
    BrowserModule,
    BrowserAnimationsModule,
    MatButtonModule,
    MatCardModule,
    MatCheckboxModule,
    MatIconModule,
    MatMenuModule,
    MatSidenavModule,
    MatSlideToggleModule,
    MatSnackBarModule,
    MatToolbarModule,
    FormsModule,
    HttpClientModule,
    KtdGridModule,
    RouterModule.forRoot([{path: '**', component: AppComponent}]),
  ],
  providers: [],
  bootstrap: [AppComponent],
})
export class AppModule {}
