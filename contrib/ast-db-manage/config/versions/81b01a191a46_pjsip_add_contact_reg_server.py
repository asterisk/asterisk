"""pjsip: add contact reg_server

Revision ID: 81b01a191a46
Revises: 65eb22eb195
Create Date: 2016-04-15 15:00:35.024525

"""

# revision identifiers, used by Alembic.
revision = '81b01a191a46'
down_revision = '65eb22eb195'

from alembic import op
import sqlalchemy as sa


def upgrade():
    op.add_column('ps_contacts', sa.Column('reg_server', sa.String(20)))
    op.create_unique_constraint('ps_contacts_uq', 'ps_contacts', ['id','reg_server'])

def downgrade():
    op.drop_constraint('ps_contacts_uq', 'ps_contacts', type_='unique')
    op.drop_column('ps_contacts', 'reg_server')
